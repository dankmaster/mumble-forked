// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancement.h"

#include "SpeechCleanup.h"
#include "SpeechCleanupProcessor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace Mumble::InputEnhancement {
namespace {
	constexpr int minimumControl = 0;
	constexpr int maximumControl = 100;

	int clampControl(int value) {
		return std::clamp(value, minimumControl, maximumControl);
	}

	int mapControlToRange(int value, int minimum, int maximum) noexcept {
		const int clamped = clampControl(value);
		return minimum + ((clamped * (maximum - minimum)) + 50) / 100;
	}

	float normalizedRangeControl(int value, int minimum, int maximum) noexcept {
		return std::clamp(static_cast< float >(value - minimum) / static_cast< float >(maximum - minimum), 0.0f,
						  1.0f);
	}

	float weightedProfileControl(const ValidatedControls &controls, int minimumReduction, int maximumReduction,
							 int minimumCharacter, int maximumCharacter) noexcept {
		const float reduction = normalizedRangeControl(controls.noiseReduction, minimumReduction, maximumReduction);
		const float character = normalizedRangeControl(controls.naturalCrisp, minimumCharacter, maximumCharacter);
		return (0.75f * reduction) + (0.25f * character);
	}

	float mixFactorForValidatedControls(Profile profile, const ValidatedControls &controls) noexcept {
		if (profile == Profile::Light) {
			// Light uses Speex only as a low-cost background estimate. Speech is
			// protected by mixClassicFrame(), while this curve controls the maximum
			// wet contribution during confidently non-speech frames.
			const float reduction = static_cast< float >(controls.noiseReduction) / 100.0f;
			const float character = static_cast< float >(controls.naturalCrisp) / 100.0f;
			return std::clamp(0.50f + (0.40f * reduction) + (0.10f * character), 0.0f, 1.0f);
		}
		if (profile == Profile::Quality) {
			// The frozen low-latency product curve stays inside the two validated
			// tuning anchors. The normal UI defaults (30 reduction, 50 clear) map
			// exactly to the selected 0.75 wet mix, while the full qualified control
			// surface interpolates monotonically from 0.70 to 0.90.
			// Public defaults 30/50 are first mapped by validatedControlsForProfile
			// to 45/63. Express the anchor in that validated coordinate system.
			constexpr int defaultValidatedReduction = 45;
			constexpr int defaultValidatedCharacter = 63;
			constexpr float nominalControl =
				(0.75f * (static_cast< float >(defaultValidatedReduction - 25) / 65.0f))
				+ (0.25f * (static_cast< float >(defaultValidatedCharacter - 25) / 75.0f));
			const float control = weightedProfileControl(controls, 25, 90, 25, 100);
			const float normalizedMix = control <= nominalControl
									? (control / nominalControl) * 0.25f
									: 0.25f + ((control - nominalControl) / (1.0f - nominalControl)) * 0.75f;
			return 0.70f + (0.20f * normalizedMix);
		}
		if (profile == Profile::VoiceFocus) {
			// Voice Focus is intentionally aggressive and explicit-only. Its normal
			// defaults use the validated 0.90 mix; only controls above that anchor
			// increase wet mix, capped at the separately qualified 0.95 endpoint.
			constexpr float nominalControl = (0.75f * 0.30f) + (0.25f * 0.50f);
			const float control = weightedProfileControl(controls, 70, 100, 40, 100);
			const float normalizedMix = control <= nominalControl
									? 0.0f
									: (control - nominalControl) / (1.0f - nominalControl);
			return 0.90f + (0.05f * normalizedMix);
		}
		const float reduction = static_cast< float >(controls.noiseReduction) / 100.0f;
		const float character = static_cast< float >(controls.naturalCrisp) / 100.0f;
		return std::clamp(reduction * (0.75f + 0.25f * character), 0.0f, 1.0f);
	}

	void appendFingerprintU32(QByteArray &bytes, const std::uint32_t value) {
		const char encoded[] = {
			static_cast< char >((value >> 24U) & 0xffU),
			static_cast< char >((value >> 16U) & 0xffU),
			static_cast< char >((value >> 8U) & 0xffU),
			static_cast< char >(value & 0xffU),
		};
		bytes.append(encoded, static_cast< qsizetype >(sizeof(encoded)));
	}

	void appendFingerprintText(QByteArray &bytes, const QString &value) {
		const QByteArray utf8 = value.toUtf8();
		appendFingerprintU32(bytes, static_cast< std::uint32_t >(utf8.size()));
		bytes.append(utf8);
	}

	QString profileSlug(Profile profile) {
		switch (profile) {
			case Profile::Original:
				return QStringLiteral("original");
			case Profile::Light:
				return QStringLiteral("light");
			case Profile::Balanced:
				return QStringLiteral("balanced");
			case Profile::Quality:
				return QStringLiteral("quality");
			case Profile::Auto:
				return QStringLiteral("auto");
			case Profile::VoiceFocus:
				return QStringLiteral("voice-focus");
		}

		return QStringLiteral("original");
	}
} // namespace

ValidatedControls validatedControlsForProfile(Profile profile, int noiseReduction, int naturalCrisp) noexcept {
	switch (profile) {
		case Profile::Original:
			return {};
		case Profile::Light:
			return { clampControl(noiseReduction), clampControl(naturalCrisp) };
		case Profile::Balanced:
			return { mapControlToRange(noiseReduction, 20, 90), mapControlToRange(naturalCrisp, 10, 90) };
		case Profile::Quality:
			return { mapControlToRange(noiseReduction, 25, 90), mapControlToRange(naturalCrisp, 25, 100) };
		case Profile::Auto:
			// Auto is a request, never a concrete processing profile. Callers that
			// process audio must pass the policy-selected concrete profile.
			return {};
		case Profile::VoiceFocus:
			return { mapControlToRange(noiseReduction, 70, 100), mapControlToRange(naturalCrisp, 40, 100) };
	}

	return {};
}

float mixFactorForControls(Profile profile, int noiseReduction, int naturalCrisp) noexcept {
	return mixFactorForValidatedControls(profile, validatedControlsForProfile(profile, noiseReduction, naturalCrisp));
}

int lightSpeexSuppressionDb(int noiseReduction) noexcept {
	const int boundedReduction = clampControl(noiseReduction);
	return -5 - ((boundedReduction * 25) + 50) / 100;
}

Recipe RecipeCatalog::makeRecipe(Profile requestedProfile, Profile effectiveProfile, int noiseReduction,
								 int naturalCrisp) {
	const ValidatedControls controls = validatedControlsForProfile(effectiveProfile, noiseReduction, naturalCrisp);

	QString id;
	Engine engine = Engine::None;
	QString modelId;
	float mixFactor            = 0.0f;
	unsigned int latencyBudget = originalLatencyBudgetSamples;
	CpuClass minimumCpuClass   = CpuClass::Low;

	switch (effectiveProfile) {
		case Profile::Original:
			id = requestedProfile == Profile::Original
					 ? QStringLiteral("input.original")
					 : QStringLiteral("input.%1.fallback-original").arg(profileSlug(requestedProfile));
			break;
		case Profile::Light:
			engine        = Engine::Speex;
			mixFactor     = mixFactorForValidatedControls(effectiveProfile, controls);
			latencyBudget = lightLatencyBudgetSamples;
			id            = requestedProfile == Profile::Light ? QStringLiteral("input.light.speex")
							: requestedProfile == Profile::Auto
								? QStringLiteral("input.auto.light.speex")
								: QStringLiteral("input.%1.fallback-light.speex").arg(profileSlug(requestedProfile));
			break;
		case Profile::Balanced:
			engine          = Engine::RNNoise;
			modelId         = QStringLiteral("rnnoise:embedded");
			mixFactor       = mixFactorForValidatedControls(effectiveProfile, controls);
			latencyBudget   = balancedLatencyBudgetSamples;
			minimumCpuClass = CpuClass::Standard;
			id              = requestedProfile == Profile::Balanced ? QStringLiteral("input.balanced.rnnoise-embedded")
							  : requestedProfile == Profile::Auto
								  ? QStringLiteral("input.auto.balanced.rnnoise-embedded")
								  : QStringLiteral("input.%1.fallback-balanced.rnnoise-embedded").arg(profileSlug(requestedProfile));
			break;
		case Profile::Quality:
			engine          = Engine::DeepFilterNet;
			modelId         = QStringLiteral("deepfilternet:low-latency");
			mixFactor       = mixFactorForValidatedControls(effectiveProfile, controls);
			latencyBudget   = qualityLatencyBudgetSamples;
			minimumCpuClass = CpuClass::High;
			id = requestedProfile == Profile::Quality ? QStringLiteral("input.quality.deepfilternet-low-latency")
													  : QStringLiteral("input.auto.quality.deepfilternet-low-latency");
			break;
		case Profile::Auto:
			// Auto is a request, never an executable recipe.
			break;
		case Profile::VoiceFocus:
			engine          = Engine::DeepFilterNet;
			modelId         = QStringLiteral("deepfilternet:low-latency");
			mixFactor       = mixFactorForValidatedControls(effectiveProfile, controls);
			latencyBudget   = voiceFocusLatencyBudgetSamples;
			minimumCpuClass = CpuClass::High;
			id              = QStringLiteral("input.voice-focus.deepfilternet-low-latency");
			break;
	}

	return Recipe(std::move(id), RecipeCatalog::currentRevision, requestedProfile, effectiveProfile, engine,
				  std::move(modelId), controls.noiseReduction, controls.naturalCrisp, mixFactor, latencyBudget,
				  minimumCpuClass);
}

namespace {

	std::unique_ptr< SpeechCleanupProcessor > defaultProcessorFactory(const Recipe &recipe) {
		Mumble::SpeechCleanup::Selection selection;
		switch (recipe.engine()) {
			case Engine::RNNoise:
				selection.backend = ::Settings::RNNoiseBackend;
				break;
			case Engine::DeepFilterNet:
				selection.backend = ::Settings::DeepFilterNetBackend;
				break;
			case Engine::DTLN:
				selection.backend = ::Settings::DTLNBackend;
				break;
			case Engine::None:
			case Engine::Speex:
				return {};
		}

		selection.modelId = recipe.modelId();
		selection.modelDomainNormalization = recipe.engine() == Engine::DeepFilterNet;
		return createSpeechCleanupProcessor(selection);
	}

	std::uint64_t steadyNanoseconds() {
		return static_cast< std::uint64_t >(
			std::chrono::duration_cast< std::chrono::nanoseconds >(std::chrono::steady_clock::now().time_since_epoch())
				.count());
	}

	bool validSha256(const QString &value) {
		return value.size() == 64 && std::all_of(value.cbegin(), value.cend(), [](QChar character) {
				   return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
						  || (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
			   });
	}

	bool sameCanonicalPath(const QString &left, const QString &right) {
		const QString canonicalLeft  = QFileInfo(left).canonicalFilePath();
		const QString canonicalRight = QFileInfo(right).canonicalFilePath();
		if (canonicalLeft.isEmpty() || canonicalRight.isEmpty()) {
			return false;
		}
#ifdef Q_OS_WIN
		return canonicalLeft.compare(canonicalRight, Qt::CaseInsensitive) == 0;
#else
		return canonicalLeft == canonicalRight;
#endif
	}

	bool fileHasSha256(const QString &path, const QString &expectedSha256) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return false;
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while (!file.atEnd()) {
			const QByteArray chunk = file.read(1024 * 1024);
			if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
				return false;
			}
			hash.addData(chunk);
		}
		return QString::fromLatin1(hash.result().toHex()) == expectedSha256;
	}
} // namespace

CpuClass manualProfileCpuClassForMetrics(const ManualProfileCapabilityMetrics &metrics) noexcept {
	if (!metrics.rnnoiseAvailable) {
		return CpuClass::Low;
	}
	if (!metrics.deepFilterNetAvailable || !metrics.qualityPipelineReady || metrics.workerFrames == 0) {
		return CpuClass::Standard;
	}
	constexpr std::uint64_t qualifiedP99Nanoseconds = 8'000'000;
	constexpr std::uint64_t catastropheNanoseconds  = 10'000'000;
	return metrics.callbackP99Nanoseconds <= qualifiedP99Nanoseconds
			   && metrics.workerP99Nanoseconds <= qualifiedP99Nanoseconds
			   && metrics.callbackMaximumNanoseconds <= catastropheNanoseconds
			   && metrics.workerMaximumNanoseconds <= catastropheNanoseconds
		   ? CpuClass::High
		   : CpuClass::Standard;
}

CpuClass cpuClassWithAuthenticatedE2EOverride(const CpuClass measured, const bool harnessEnabled,
											  const bool tokenPresent, const QString &requestedTier) noexcept {
	if (!harnessEnabled || !tokenPresent) {
		return measured;
	}
	if (requestedTier == QLatin1String("Low")) {
		return CpuClass::Low;
	}
	if (requestedTier == QLatin1String("Standard")) {
		return CpuClass::Standard;
	}
	if (requestedTier == QLatin1String("High")) {
		return CpuClass::High;
	}
	return measured;
}

BackendAvailability BackendAvailability::compiled() {
	return {
		Mumble::SpeechCleanup::isBackendAvailable(::Settings::RNNoiseBackend),
		Mumble::SpeechCleanup::isBackendAvailable(::Settings::DeepFilterNetBackend),
		Mumble::SpeechCleanup::isBackendAvailable(::Settings::DTLNBackend),
	};
}

CaptureDeviceContext CaptureDeviceContext::liveDevice(QString backendId, const bool stablePhysicalIdentity) {
	CaptureDeviceContext context;
	context.kind                   = Kind::LiveDevice;
	context.backendId              = std::move(backendId);
	context.stablePhysicalIdentity = stablePhysicalIdentity;
	return context;
}

bool CaptureDeviceContext::productionQualified() const noexcept {
#ifdef Q_OS_WIN
	return kind == Kind::LiveDevice && stablePhysicalIdentity
		   && backendId.compare(QLatin1String("WASAPI"), Qt::CaseInsensitive) == 0;
#else
	return false;
#endif
}

ProfileReadiness profileReadiness(const ResolveRequest &request) noexcept {
	const auto cpuAtLeast = [actual = request.cpuClass](CpuClass required) {
		return static_cast< std::uint8_t >(actual) >= static_cast< std::uint8_t >(required);
	};
	ProfileReadiness readiness;
	readiness.selectable          = true;
	readiness.productionQualified = request.captureDevice.productionQualified();
	readiness.reason              = ProfileReadinessReason::Ready;

	switch (request.profile) {
		case Profile::Original:
		case Profile::Light:
			return readiness;
		case Profile::Balanced:
			if (!cpuAtLeast(CpuClass::Standard)) {
				return { false, false, ProfileReadinessReason::InsufficientCpu };
			}
			if (!request.backendAvailability.rnnoise) {
				return { false, false, ProfileReadinessReason::BackendUnavailable };
			}
			return readiness;
		case Profile::Quality:
		case Profile::VoiceFocus:
			if (!cpuAtLeast(CpuClass::High)) {
				return { false, false, ProfileReadinessReason::InsufficientCpu };
			}
			if (!request.backendAvailability.deepFilterNet) {
				return { false, false, ProfileReadinessReason::BackendUnavailable };
			}
			return readiness;
		case Profile::Auto:
			// AutoV2's policy, binding, diagnostics and transition math are
			// available, but PreparedPipelineBank cannot yet lease source and
			// candidate processors across commit/abort. Keep the option visible but
			// fail closed until the live dual-pipeline contract exists.
			return { false, false, ProfileReadinessReason::AutoRuntimeUnavailable };
	}

	return { false, false, ProfileReadinessReason::BackendUnavailable };
}

Recipe::Recipe(QString id, std::uint32_t revision, Profile requestedProfile, Profile effectiveProfile, Engine engine,
			   QString modelId, int noiseReduction, int naturalCrisp, float mixFactor,
			   unsigned int latencyBudgetSamples, CpuClass minimumCpuClass)
	: m_id(std::move(id)), m_revision(revision), m_requestedProfile(requestedProfile),
	  m_effectiveProfile(effectiveProfile), m_engine(engine), m_modelId(std::move(modelId)),
	  m_noiseReduction(noiseReduction), m_naturalCrisp(naturalCrisp), m_mixFactor(mixFactor),
	  m_latencyBudgetSamples(latencyBudgetSamples), m_minimumCpuClass(minimumCpuClass) {
}

const QString &Recipe::id() const noexcept {
	return m_id;
}

std::uint32_t Recipe::revision() const noexcept {
	return m_revision;
}

Profile Recipe::requestedProfile() const noexcept {
	return m_requestedProfile;
}

Profile Recipe::effectiveProfile() const noexcept {
	return m_effectiveProfile;
}

Engine Recipe::engine() const noexcept {
	return m_engine;
}

const QString &Recipe::modelId() const noexcept {
	return m_modelId;
}

int Recipe::noiseReduction() const noexcept {
	return m_noiseReduction;
}

int Recipe::naturalCrisp() const noexcept {
	return m_naturalCrisp;
}

float Recipe::mixFactor() const noexcept {
	return m_mixFactor;
}

unsigned int Recipe::latencyBudgetSamples() const noexcept {
	return m_latencyBudgetSamples;
}

CpuClass Recipe::minimumCpuClass() const noexcept {
	return m_minimumCpuClass;
}

bool Recipe::usesNeuralProcessor() const noexcept {
	return m_engine == Engine::RNNoise || m_engine == Engine::DeepFilterNet || m_engine == Engine::DTLN;
}

QString recipeExecutionFingerprint(const Recipe &recipe) {
	static_assert(sizeof(float) == sizeof(std::uint32_t));
	static_assert(std::numeric_limits< float >::is_iec559);

	QByteArray canonical("mumble-input-enhancement-execution\0", 35);
	appendFingerprintU32(canonical, Recipe::schemaVersion);
	appendFingerprintU32(canonical, recipeExecutionSemanticsVersion);
	appendFingerprintU32(canonical, qualifiedMixCurveVersion);
	appendFingerprintU32(canonical, adaptationPolicyVersion);
	appendFingerprintText(canonical, recipe.id());
	appendFingerprintU32(canonical, recipe.revision());
	appendFingerprintU32(canonical, static_cast< std::uint32_t >(recipe.requestedProfile()));
	appendFingerprintU32(canonical, static_cast< std::uint32_t >(recipe.effectiveProfile()));
	appendFingerprintU32(canonical, static_cast< std::uint32_t >(recipe.engine()));
	appendFingerprintText(canonical, recipe.modelId());
	appendFingerprintU32(canonical, static_cast< std::uint32_t >(recipe.noiseReduction()));
	appendFingerprintU32(canonical, static_cast< std::uint32_t >(recipe.naturalCrisp()));
	appendFingerprintU32(canonical, std::bit_cast< std::uint32_t >(recipe.mixFactor()));
	appendFingerprintU32(canonical, recipe.latencyBudgetSamples());
	appendFingerprintU32(canonical, static_cast< std::uint32_t >(recipe.minimumCpuClass()));
	return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

Recipe RecipeCatalog::resolve(const ResolveRequest &request) {
	Profile effectiveProfile = request.profile;

	if (request.profile == Profile::Balanced && !request.backendAvailability.rnnoise) {
		effectiveProfile = Profile::Light;
	} else if ((request.profile == Profile::Quality || request.profile == Profile::VoiceFocus)
			   && !request.backendAvailability.deepFilterNet) {
		effectiveProfile = request.backendAvailability.rnnoise ? Profile::Balanced : Profile::Light;
	} else if (request.profile == Profile::Auto) {
		if (request.cpuClass == CpuClass::High && request.backendAvailability.deepFilterNet) {
			effectiveProfile = Profile::Quality;
		} else if (request.cpuClass != CpuClass::Low && request.backendAvailability.rnnoise) {
			effectiveProfile = Profile::Balanced;
		} else {
			effectiveProfile = Profile::Light;
		}
	}

	return makeRecipe(request.profile, effectiveProfile, request.noiseReduction, request.naturalCrisp);
}

Diagnostics::Diagnostics(QString requestedRecipeId, std::uint32_t recipeRevision, Profile requestedProfile,
						 Profile activeProfile, Engine activeEngine, QString activeModelId, QString activeModelSha256,
						 unsigned int latencyBudgetSamples, unsigned int actualLatencySamples,
						 std::uint64_t processedFrames, std::uint64_t neuralFrames, std::uint64_t sanitizedInputSamples,
						 std::uint64_t clampedInputSamples, std::uint64_t clampedOutputSamples,
						 std::uint64_t deadlineMisses, std::uint64_t fallbackCount,
						 std::uint64_t totalProcessingNanoseconds, std::uint64_t maximumProcessingNanoseconds,
						 std::uint64_t processingP50Nanoseconds, std::uint64_t processingP95Nanoseconds,
						 std::uint64_t processingP99Nanoseconds, std::uint64_t workerProcessingFrames,
						 std::uint64_t workerTotalProcessingNanoseconds,
						 std::uint64_t workerMaximumProcessingNanoseconds, std::uint64_t workerProcessingP99Nanoseconds,
						 bool fallbackActive, FallbackReason fallbackReason)
	: m_requestedRecipeId(std::move(requestedRecipeId)), m_recipeRevision(recipeRevision),
	  m_requestedProfile(requestedProfile), m_activeProfile(activeProfile), m_activeEngine(activeEngine),
	  m_activeModelId(std::move(activeModelId)), m_activeModelSha256(std::move(activeModelSha256)),
	  m_latencyBudgetSamples(latencyBudgetSamples), m_actualLatencySamples(actualLatencySamples),
	  m_processedFrames(processedFrames), m_neuralFrames(neuralFrames), m_sanitizedInputSamples(sanitizedInputSamples),
	  m_clampedInputSamples(clampedInputSamples), m_clampedOutputSamples(clampedOutputSamples),
	  m_deadlineMisses(deadlineMisses), m_fallbackCount(fallbackCount),
	  m_totalProcessingNanoseconds(totalProcessingNanoseconds),
	  m_maximumProcessingNanoseconds(maximumProcessingNanoseconds),
	  m_processingP50Nanoseconds(processingP50Nanoseconds), m_processingP95Nanoseconds(processingP95Nanoseconds),
	  m_processingP99Nanoseconds(processingP99Nanoseconds), m_workerProcessingFrames(workerProcessingFrames),
	  m_workerTotalProcessingNanoseconds(workerTotalProcessingNanoseconds),
	  m_workerMaximumProcessingNanoseconds(workerMaximumProcessingNanoseconds),
	  m_workerProcessingP99Nanoseconds(workerProcessingP99Nanoseconds), m_fallbackActive(fallbackActive),
	  m_fallbackReason(fallbackReason) {
}

const QString &Diagnostics::requestedRecipeId() const noexcept {
	return m_requestedRecipeId;
}
std::uint32_t Diagnostics::recipeRevision() const noexcept {
	return m_recipeRevision;
}
Profile Diagnostics::requestedProfile() const noexcept {
	return m_requestedProfile;
}
Profile Diagnostics::activeProfile() const noexcept {
	return m_activeProfile;
}
Engine Diagnostics::activeEngine() const noexcept {
	return m_activeEngine;
}
const QString &Diagnostics::activeModelId() const noexcept {
	return m_activeModelId;
}
const QString &Diagnostics::activeModelSha256() const noexcept {
	return m_activeModelSha256;
}
unsigned int Diagnostics::latencyBudgetSamples() const noexcept {
	return m_latencyBudgetSamples;
}
unsigned int Diagnostics::actualLatencySamples() const noexcept {
	return m_actualLatencySamples;
}
std::uint64_t Diagnostics::processedFrames() const noexcept {
	return m_processedFrames;
}
std::uint64_t Diagnostics::neuralFrames() const noexcept {
	return m_neuralFrames;
}
std::uint64_t Diagnostics::sanitizedInputSamples() const noexcept {
	return m_sanitizedInputSamples;
}
std::uint64_t Diagnostics::clampedInputSamples() const noexcept {
	return m_clampedInputSamples;
}
std::uint64_t Diagnostics::clampedOutputSamples() const noexcept {
	return m_clampedOutputSamples;
}
std::uint64_t Diagnostics::deadlineMisses() const noexcept {
	return m_deadlineMisses;
}
std::uint64_t Diagnostics::fallbackCount() const noexcept {
	return m_fallbackCount;
}
std::uint64_t Diagnostics::totalProcessingNanoseconds() const noexcept {
	return m_totalProcessingNanoseconds;
}
std::uint64_t Diagnostics::maximumProcessingNanoseconds() const noexcept {
	return m_maximumProcessingNanoseconds;
}
std::uint64_t Diagnostics::processingP50Nanoseconds() const noexcept {
	return m_processingP50Nanoseconds;
}
std::uint64_t Diagnostics::processingP95Nanoseconds() const noexcept {
	return m_processingP95Nanoseconds;
}
std::uint64_t Diagnostics::processingP99Nanoseconds() const noexcept {
	return m_processingP99Nanoseconds;
}
std::uint64_t Diagnostics::workerProcessingFrames() const noexcept {
	return m_workerProcessingFrames;
}
std::uint64_t Diagnostics::workerTotalProcessingNanoseconds() const noexcept {
	return m_workerTotalProcessingNanoseconds;
}
std::uint64_t Diagnostics::workerMaximumProcessingNanoseconds() const noexcept {
	return m_workerMaximumProcessingNanoseconds;
}
std::uint64_t Diagnostics::workerProcessingP99Nanoseconds() const noexcept {
	return m_workerProcessingP99Nanoseconds;
}
bool Diagnostics::fallbackActive() const noexcept {
	return m_fallbackActive;
}
FallbackReason Diagnostics::fallbackReason() const noexcept {
	return m_fallbackReason;
}

Pipeline::Pipeline(ProcessorFactory processorFactory, NanosecondClock clock, std::uint64_t frameDeadlineNanoseconds)
	: m_processorFactory(processorFactory ? std::move(processorFactory) : ProcessorFactory(defaultProcessorFactory)),
	  m_clock(clock ? std::move(clock) : NanosecondClock(steadyNanoseconds)),
	  m_frameDeadlineNanoseconds(frameDeadlineNanoseconds),
	  m_recipe(std::make_unique< Recipe >(RecipeCatalog::resolve({}))) {
}

Pipeline::~Pipeline() = default;

bool Pipeline::configure(const Recipe &recipe, const QString &authorizedModelSha256,
						 const QString &authorizedModelPath) {
	m_processor.reset();
	m_recipe = std::make_unique< Recipe >(recipe);
	m_alignedDryFrame.fill(0.0f);
	m_alignedDryDelay.fill(0.0f);
	m_alignedDryDelayPosition = 0;
	m_activeModelId.clear();
	m_activeModelSha256.clear();
	// Speex preprocessing is causal but emits the preceding 10 ms frame. Treat
	// that delay as part of the product contract so fixed-timeline scoring and
	// utterance tail drain cannot hide or discard it. Original remains exactly
	// zero-latency and neural processors publish their measured latency below.
	m_actualLatencySamples = recipe.engine() == Engine::Speex ? frameSamples : 0;
	m_fallbackActive       = false;
	m_fallbackReason       = FallbackReason::None;
	resetCounters();
	resetSpeechEdgeProtection();
	resetClassicMix();

	if (!recipe.usesNeuralProcessor()) {
		if (m_actualLatencySamples > recipe.latencyBudgetSamples()) {
			failClosed(FallbackReason::LatencyBudgetExceeded);
			return false;
		}
		return true;
	}

	const QString normalizedModelSha256 = authorizedModelSha256.trimmed().toLower();
	const QString normalizedModelPath   = authorizedModelPath.trimmed();
	const bool hasModelAuthorization    = !normalizedModelSha256.isEmpty() || !normalizedModelPath.isEmpty();
	if (hasModelAuthorization
		&& (!validSha256(normalizedModelSha256) || normalizedModelPath.isEmpty()
			|| !fileHasSha256(normalizedModelPath, normalizedModelSha256))) {
		// Re-hash the authorized asset immediately before the processor factory is
		// allowed to parse or initialize it. The second check below binds the
		// factory's actual loaded path and catches any change during initialization.
		failClosed(FallbackReason::UnexpectedModel);
		return false;
	}

	std::unique_ptr< SpeechCleanupProcessor > processor = m_processorFactory(recipe);
	if (!processor) {
		failClosed(FallbackReason::ProcessorUnavailable);
		return false;
	}
	if (!processor->isReady()) {
		failClosed(FallbackReason::ProcessorNotReady);
		return false;
	}
	if (processor->usedFallback()) {
		failClosed(FallbackReason::ProcessorFallback);
		return false;
	}

	const QString activeModelId = processor->activeModelId();
	if (!activeModelId.isEmpty() && activeModelId != recipe.modelId()) {
		failClosed(FallbackReason::UnexpectedModel);
		return false;
	}

	const unsigned int warmupLatencySamples = processor->latencySamples();
	if (warmupLatencySamples > recipe.latencyBudgetSamples() || warmupLatencySamples > m_alignedDryDelay.size()) {
		failClosed(FallbackReason::LatencyBudgetExceeded);
		return false;
	}

	// Force lazy model/runtime initialization before the audio callback sees the
	// processor. The warmup is deliberately excluded from callback diagnostics,
	// then reset so neither synthetic samples nor causal tail enter user audio.
	std::array< float, frameSamples > warmupFrame = {};
	try {
		for (unsigned int frame = 0; frame < processorWarmupFrames; ++frame) {
			warmupFrame.fill(0.0f);
			processor->processInPlace(warmupFrame.data(), frameSamples, recipe.mixFactor());
			for (float sample : warmupFrame) {
				if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f) {
					failClosed(FallbackReason::InvalidOutput);
					return false;
				}
			}
		}
		processor->reset();
		// Some engines recreate their inference state in reset(). Exercise that
		// freshly reset state once as well, so lazy initialization cannot move
		// back to the first capture callback. Only zero state/tail remains.
		for (unsigned int frame = 0; frame < processorPostResetProbeFrames; ++frame) {
			warmupFrame.fill(0.0f);
			processor->processInPlace(warmupFrame.data(), frameSamples, recipe.mixFactor());
			for (float sample : warmupFrame) {
				if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f) {
					failClosed(FallbackReason::InvalidOutput);
					return false;
				}
			}
		}
		// The probe above warms code paths and allocations, but its synthetic
		// zero frame must not become hidden algorithmic pre-roll. Reset once more
		// so a declared cold-start run begins from the engine's exact zero state.
		processor->reset();
		if (!processor->prepareRealtime()) {
			failClosed(FallbackReason::ProcessorNotReady);
			return false;
		}
	} catch (...) {
		failClosed(FallbackReason::ProcessorException);
		return false;
	}
	if (!processor->isReady()) {
		failClosed(FallbackReason::ProcessorNotReady);
		return false;
	}
	if (processor->usedFallback()) {
		failClosed(FallbackReason::ProcessorFallback);
		return false;
	}
	const QString finalActiveModelId = processor->activeModelId();
	if (!finalActiveModelId.isEmpty() && finalActiveModelId != recipe.modelId()) {
		failClosed(FallbackReason::UnexpectedModel);
		return false;
	}
	if (hasModelAuthorization) {
		const QString activeModelPath = processor->activeModelPath();
		if (!sameCanonicalPath(activeModelPath, normalizedModelPath)
			|| !fileHasSha256(activeModelPath, normalizedModelSha256)) {
			failClosed(FallbackReason::UnexpectedModel);
			return false;
		}
		m_activeModelSha256 = normalizedModelSha256;
	}
	const unsigned int actualLatencySamples = processor->latencySamples();
	if (actualLatencySamples > recipe.latencyBudgetSamples() || actualLatencySamples > m_alignedDryDelay.size()) {
		failClosed(FallbackReason::LatencyBudgetExceeded);
		return false;
	}
	m_alignedDryFrame.fill(0.0f);
	m_alignedDryDelay.fill(0.0f);
	m_alignedDryDelayPosition = 0;

	m_activeModelId        = finalActiveModelId.isEmpty() ? recipe.modelId() : finalActiveModelId;
	m_actualLatencySamples = actualLatencySamples;
	m_processor            = std::move(processor);
	return true;
}

bool Pipeline::prepareOfflineFrame() noexcept {
	if (!m_recipe || !m_recipe->usesNeuralProcessor()) {
		return true;
	}
	if (m_fallbackActive) {
		return false;
	}
	if (!m_processor) {
		failClosed(FallbackReason::ProcessorUnavailable);
		return false;
	}
	if (!m_processor->prepareOfflineFrame() || !m_processor->isReady()) {
		failClosed(FallbackReason::ProcessorNotReady);
		return false;
	}
	if (m_processor->usedFallback()) {
		failClosed(FallbackReason::ProcessorFallback);
		return false;
	}
	return true;
}

bool Pipeline::finishOfflineProcessing() noexcept {
	if (!m_recipe || !m_recipe->usesNeuralProcessor()) {
		return true;
	}
	if (m_fallbackActive) {
		return false;
	}
	if (!m_processor) {
		failClosed(FallbackReason::ProcessorUnavailable);
		return false;
	}
	if (!m_processor->finishOfflineProcessing() || !m_processor->isReady()) {
		failClosed(FallbackReason::ProcessorNotReady);
		return false;
	}
	if (m_processor->usedFallback()) {
		failClosed(FallbackReason::ProcessorFallback);
		return false;
	}
	return true;
}

bool Pipeline::processFrame(float *samples, unsigned int sampleCount) noexcept {
	return processFrame(samples, sampleCount, m_recipe ? m_recipe->mixFactor() : 0.0f);
}

bool Pipeline::processFrame(float *samples, unsigned int sampleCount, float mixFactor) noexcept {
	++m_processedFrames;
	m_lastProcessingNanoseconds = 0;

	if (!m_recipe || !m_recipe->usesNeuralProcessor()) {
		return false;
	}
	if (!samples || sampleCount != frameSamples) {
		failClosed(FallbackReason::InvalidFrame);
		return false;
	}
	const bool measureProcessing  = !m_fallbackActive && static_cast< bool >(m_processor);
	const std::uint64_t startedAt = measureProcessing ? m_clock() : 0;

	for (unsigned int i = 0; i < frameSamples; ++i) {
		float sample = samples[i];
		if (!std::isfinite(sample)) {
			sample = 0.0f;
			++m_sanitizedInputSamples;
		} else if (sample < -1.0f || sample > 1.0f) {
			sample = std::clamp(sample, -1.0f, 1.0f);
			++m_clampedInputSamples;
		}
		samples[i] = sample;

		if (m_actualLatencySamples == 0) {
			m_alignedDryFrame[i] = sample;
		} else {
			m_alignedDryFrame[i]                         = m_alignedDryDelay[m_alignedDryDelayPosition];
			m_alignedDryDelay[m_alignedDryDelayPosition] = sample;
			m_alignedDryDelayPosition                    = (m_alignedDryDelayPosition + 1) % m_actualLatencySamples;
		}
	}

	// A runtime failure must not jump from the processor's delayed timeline to
	// the current, zero-latency input frame. Keep accepting audio (and the
	// caller's zero-input tail drain) on the established timeline instead.
	if (m_fallbackActive) {
		std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
		return false;
	}
	if (!m_processor) {
		std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
		failClosed(FallbackReason::ProcessorUnavailable);
		return false;
	}

	auto finishProcessingTiming = [this, startedAt]() noexcept {
		const std::uint64_t finishedAt         = m_clock();
		const std::uint64_t elapsedNanoseconds = finishedAt >= startedAt ? finishedAt - startedAt : 0;
		m_lastProcessingNanoseconds            = elapsedNanoseconds;
		recordProcessingDuration(elapsedNanoseconds);
		++m_neuralFrames;
		return elapsedNanoseconds;
	};
	const float protectedMixFactor = speechEdgeProtectedMixFactor(mixFactor);
	try {
		m_processor->processInPlace(samples, frameSamples, protectedMixFactor);
	} catch (...) {
		finishProcessingTiming();
		std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
		failClosed(FallbackReason::ProcessorException);
		return false;
	}

	// A worker-backed processor reports an asynchronous model/queue failure by
	// becoming unhealthy. Check this immediately, before accepting finite stale
	// output as a successful callback.
	if (!m_processor->isReady()) {
		finishProcessingTiming();
		std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
		failClosed(FallbackReason::ProcessorNotReady);
		return false;
	}
	if (m_processor->usedFallback()) {
		finishProcessingTiming();
		std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
		failClosed(FallbackReason::ProcessorFallback);
		return false;
	}

	for (unsigned int i = 0; i < frameSamples; ++i) {
		if (!std::isfinite(samples[i])) {
			finishProcessingTiming();
			std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
			failClosed(FallbackReason::InvalidOutput);
			return false;
		}
		if (samples[i] < -1.0f || samples[i] > 1.0f) {
			samples[i] = std::clamp(samples[i], -1.0f, 1.0f);
			++m_clampedOutputSamples;
		}
	}

	const std::uint64_t elapsedNanoseconds = finishProcessingTiming();
	if (elapsedNanoseconds > m_frameDeadlineNanoseconds) {
		++m_deadlineMisses;
		std::copy(m_alignedDryFrame.cbegin(), m_alignedDryFrame.cend(), samples);
		failClosed(FallbackReason::DeadlineExceeded);
		return false;
	}

	return true;
}

void Pipeline::resetSpeechEdgeProtection() noexcept {
	m_speechEdgeNoiseFloorRms       = 0.0f;
	m_speechEdgePeakRms             = 0.0f;
	m_speechEdgeBaselineFrames      = 0;
	m_speechEdgeBelowReleaseFrames  = 0;
	m_speechEdgeProtectionFrame     = deepFilterOnsetRampFrames;
	m_speechEdgeActive              = false;
}

float Pipeline::speechEdgeProtectedMixFactor(float requestedMixFactor) noexcept {
	const float boundedMixFactor = std::clamp(requestedMixFactor, 0.0f, 1.0f);
	if (!m_recipe) {
		return boundedMixFactor;
	}
	const bool balancedProfile = m_recipe->effectiveProfile() == Profile::Balanced;
	const bool deepFilterProfile = m_recipe->effectiveProfile() == Profile::Quality
								   || m_recipe->effectiveProfile() == Profile::VoiceFocus;
	if (!balancedProfile && !deepFilterProfile) {
		return boundedMixFactor;
	}

	// Neural denoisers can suppress low-energy consonants while their speech
	// estimate rises from a quiet background. Detect that edge on the already
	// delayed dry frame, which is aligned to the model output being mixed in this
	// callback. This adds no look-ahead or latency. Balanced preserves exactly
	// the first 10 ms edge frame; DeepFilterNet uses its longer bounded ramp and
	// quiet-room release guard.
	double energy = 0.0;
	for (const float sample : m_alignedDryFrame) {
		energy += static_cast< double >(sample) * static_cast< double >(sample);
	}
	const float rms = static_cast< float >(std::sqrt(energy / static_cast< double >(frameSamples)));

	constexpr float absoluteNoiseFloorRms = 0.0000316227766f; // -90 dBFS
	constexpr float onsetToFloorRatio      = 2.0f;
	constexpr float releaseToFloorRatio    = 1.5f;
	constexpr unsigned int baselineFramesRequired = 3;
	constexpr unsigned int releaseFramesRequired  = 5;
	constexpr float releaseToSpeechPeakRatio = 0.25f;
	constexpr float quietRoomToSpeechPeakRatio = 0.10f;
	constexpr std::array< float, deepFilterOnsetRampFrames > onsetMixCaps = {
		0.0f, 0.0f, 0.0f, 0.0f, 0.10f, 0.25f, 0.45f, 0.70f
	};

	const float learnedFloor = m_speechEdgeNoiseFloorRms > 0.0f ? m_speechEdgeNoiseFloorRms
													   : absoluteNoiseFloorRms;
	const float onsetThreshold = std::max(absoluteNoiseFloorRms, learnedFloor * onsetToFloorRatio);
	const float releaseThreshold = std::max(absoluteNoiseFloorRms, learnedFloor * releaseToFloorRatio);
	bool protectRelease = false;

	if (!m_speechEdgeActive) {
		if (m_speechEdgeBaselineFrames >= baselineFramesRequired && rms > onsetThreshold) {
			m_speechEdgeActive             = true;
			m_speechEdgePeakRms            = rms;
			m_speechEdgeBelowReleaseFrames = 0;
			m_speechEdgeProtectionFrame    = 0;
		} else {
			// Before the first edge, learn the current stable room floor quickly.
			// Once armed, only follow values that still look like background so a
			// rising phoneme cannot move the threshold out of its own way.
			if (m_speechEdgeBaselineFrames == 0) {
				m_speechEdgeNoiseFloorRms = rms;
			} else if (rms <= onsetThreshold) {
				m_speechEdgeNoiseFloorRms = (m_speechEdgeNoiseFloorRms * 0.9f) + (rms * 0.1f);
			}
			if (m_speechEdgeBaselineFrames < baselineFramesRequired) {
				++m_speechEdgeBaselineFrames;
			}
		}
	} else {
		m_speechEdgePeakRms = std::max(m_speechEdgePeakRms, rms);
		const float lowEnergyThreshold = std::max(releaseThreshold,
			m_speechEdgePeakRms * releaseToSpeechPeakRatio);
		const bool quietRoom = learnedFloor <= std::max(absoluteNoiseFloorRms,
			m_speechEdgePeakRms * quietRoomToSpeechPeakRatio);
		protectRelease = quietRoom && rms <= lowEnergyThreshold;
		if (rms <= releaseThreshold) {
			++m_speechEdgeBelowReleaseFrames;
			if (m_speechEdgeBelowReleaseFrames >= releaseFramesRequired) {
				m_speechEdgeActive             = false;
				m_speechEdgePeakRms            = 0.0f;
				m_speechEdgeBaselineFrames      = 0;
				m_speechEdgeBelowReleaseFrames  = 0;
				m_speechEdgeProtectionFrame     = deepFilterOnsetRampFrames;
				m_speechEdgeNoiseFloorRms        = rms;
			}
		} else {
			m_speechEdgeBelowReleaseFrames = 0;
		}
	}

	if (m_speechEdgeProtectionFrame < deepFilterOnsetRampFrames) {
		if (balancedProfile) {
			// RNNoise's causal output and its internal dry delay are already aligned
			// here. One dry frame preserves the initial consonant without extending
			// the declared 30 ms timeline or weakening the rest of the recipe.
			m_speechEdgeProtectionFrame = deepFilterOnsetRampFrames;
			return 0.0f;
		}
		const float protectedMix = std::min(boundedMixFactor, onsetMixCaps[m_speechEdgeProtectionFrame]);
		++m_speechEdgeProtectionFrame;
		return protectedMix;
	}
	if (deepFilterProfile && protectRelease) {
		return 0.0f;
	}
	return boundedMixFactor;
}

bool Pipeline::processFrame(std::array< float, frameSamples > &samples) noexcept {
	return processFrame(samples.data(), static_cast< unsigned int >(samples.size()));
}

bool Pipeline::processFrame(std::array< float, frameSamples > &samples, float mixFactor) noexcept {
	return processFrame(samples.data(), static_cast< unsigned int >(samples.size()), mixFactor);
}

void Pipeline::resetClassicMix() noexcept {
	m_classicPreviousSpeechProbability = 100;
	m_classicPreviousNoisePsdSum       = 0;
	m_classicRmsHistogram.fill(0);
	m_classicSpeechProbabilityHistogram.fill(0);
	m_classicRmsHistogramCount  = 0;
	m_classicRmsHistogramFrames = 0;
	m_classicSmoothedNoisePsdSum = 0.0f;
	m_classicWetMix              = 0.0f;
}

bool Pipeline::mixClassicFrame(std::int16_t *processedSamples, const std::int16_t *currentDrySamples,
							   unsigned int sampleCount, int currentSpeechProbability,
							   std::uint64_t currentNoisePsdSum) noexcept {
	return mixClassicFrame(processedSamples, currentDrySamples, sampleCount, currentSpeechProbability,
					   currentNoisePsdSum, m_recipe ? m_recipe->mixFactor() : 0.0f);
}

bool Pipeline::mixClassicFrame(std::int16_t *processedSamples, const std::int16_t *currentDrySamples,
							   unsigned int sampleCount, int currentSpeechProbability,
							   std::uint64_t currentNoisePsdSum, float mixFactor) noexcept {
	if (!m_recipe || m_recipe->engine() != Engine::Speex || m_recipe->usesNeuralProcessor() || !processedSamples
		|| !currentDrySamples || sampleCount != frameSamples || m_actualLatencySamples != frameSamples) {
		failClosed(FallbackReason::InvalidFrame);
		return false;
	}

	for (unsigned int index = 0; index < frameSamples; ++index) {
		const float dry = static_cast< float >(currentDrySamples[index]) / 32768.0f;
		m_alignedDryFrame[index]                         = m_alignedDryDelay[m_alignedDryDelayPosition];
		m_alignedDryDelay[m_alignedDryDelayPosition]     = dry;
		m_alignedDryDelayPosition = (m_alignedDryDelayPosition + 1) % m_actualLatencySamples;
	}

	const int alignedSpeechProbability       = m_classicPreviousSpeechProbability;
	const std::uint64_t alignedNoisePsdSum   = m_classicPreviousNoisePsdSum;
	m_classicPreviousSpeechProbability = std::clamp(currentSpeechProbability, 0, 100);
	m_classicPreviousNoisePsdSum = currentNoisePsdSum;

	double dryEnergy = 0.0;
	for (const float sample : m_alignedDryFrame) {
		dryEnergy += static_cast< double >(sample) * static_cast< double >(sample);
	}
	const float dryRms = static_cast< float >(std::sqrt(dryEnergy / static_cast< double >(frameSamples)));
	const float dryRmsDb = 20.0f * std::log10(std::max(dryRms, 0.000001f));
	const std::size_t dryRmsBin = static_cast< std::size_t >(
		std::clamp(static_cast< int >(std::lround(dryRmsDb + 120.0f)), 0, 120));
	++m_classicRmsHistogram[dryRmsBin];
	++m_classicSpeechProbabilityHistogram[static_cast< std::size_t >(alignedSpeechProbability)];
	++m_classicRmsHistogramCount;
	++m_classicRmsHistogramFrames;
	if ((m_classicRmsHistogramFrames % 256U) == 0U) {
		m_classicRmsHistogramCount = 0;
		for (std::uint16_t &count : m_classicRmsHistogram) {
			count = static_cast< std::uint16_t >((count + 1U) / 2U);
			m_classicRmsHistogramCount += count;
		}
		for (std::uint16_t &count : m_classicSpeechProbabilityHistogram) {
			count = static_cast< std::uint16_t >((count + 1U) / 2U);
		}
	}
	m_classicSmoothedNoisePsdSum = m_classicRmsHistogramFrames == 1
		? static_cast< float >(alignedNoisePsdSum)
		: (0.95f * m_classicSmoothedNoisePsdSum) + (0.05f * static_cast< float >(alignedNoisePsdSum));
	if (m_fallbackActive) {
		for (unsigned int index = 0; index < frameSamples; ++index) {
			const float scaled = std::clamp(m_alignedDryFrame[index] * 32768.0f, -32768.0f, 32767.0f);
			processedSamples[index] = static_cast< std::int16_t >(std::lrint(scaled));
		}
		return false;
	}

	auto histogramPercentileBin = [this](unsigned int percentile) noexcept {
		const std::uint32_t target =
			std::max(1U, (m_classicRmsHistogramCount * percentile + 99U) / 100U);
		std::uint32_t cumulative = 0;
		for (std::size_t index = 0; index < m_classicRmsHistogram.size(); ++index) {
			cumulative += m_classicRmsHistogram[index];
			if (cumulative >= target) {
				return static_cast< unsigned int >(index);
			}
		}
		return static_cast< unsigned int >(m_classicRmsHistogram.size() - 1U);
	};
	const unsigned int rmsP10Bin = histogramPercentileBin(10);
	const unsigned int rmsP90Bin = histogramPercentileBin(90);
	auto speechProbabilityPercentile = [this](unsigned int percentile) noexcept {
		std::uint32_t histogramCount = 0;
		for (const std::uint16_t count : m_classicSpeechProbabilityHistogram) {
			histogramCount += count;
		}
		const std::uint32_t target =
			std::max(1U, (histogramCount * percentile + 99U) / 100U);
		std::uint32_t cumulative = 0;
		for (std::size_t index = 0; index < m_classicSpeechProbabilityHistogram.size(); ++index) {
			cumulative += m_classicSpeechProbabilityHistogram[index];
			if (cumulative >= target) {
				return static_cast< unsigned int >(index);
			}
		}
		return 100U;
	};
	const unsigned int speechProbabilityP10 = speechProbabilityPercentile(10);
	const unsigned int rmsDynamicRangeDb = rmsP90Bin >= rmsP10Bin ? rmsP90Bin - rmsP10Bin : 0;
	constexpr std::uint32_t sceneWarmupFrames = 64;
	constexpr unsigned int noisyDynamicRangeDb = 30;
	constexpr float noisyPsdSumThreshold = 8.0f;
	const bool noisyScene = m_classicRmsHistogramFrames >= sceneWarmupFrames
		&& rmsDynamicRangeDb <= noisyDynamicRangeDb && m_classicSmoothedNoisePsdSum >= noisyPsdSumThreshold;

	const float maximumWetMix = std::clamp(mixFactor, 0.0f, 1.0f);
	constexpr int fullBackgroundProbability = 35;
	constexpr int protectedSpeechProbability = 80;
	constexpr float protectedSpeechWetFraction = 0.0f;
	float speechProtection = 0.0f;
	if (alignedSpeechProbability >= protectedSpeechProbability) {
		speechProtection = 1.0f;
	} else if (alignedSpeechProbability > fullBackgroundProbability) {
		speechProtection = static_cast< float >(alignedSpeechProbability - fullBackgroundProbability)
			/ static_cast< float >(protectedSpeechProbability - fullBackgroundProbability);
	}
	const float protectedWetMix = maximumWetMix * protectedSpeechWetFraction;
	float targetWetMix = noisyScene
		? maximumWetMix + (speechProtection * (protectedWetMix - maximumWetMix))
		: 0.0f;
	// In an attested noisy scene, retain enough suppression through persistent
	// traffic or competing speech for the background estimate to remain useful.
	// Quiet scenes stay exactly delayed-dry, independent of VAD mistakes.
	if (noisyScene) {
		const bool persistentHighProbability = speechProbabilityP10 >= 45U;
		if (persistentHighProbability) {
			targetWetMix = std::max(targetWetMix, maximumWetMix * 0.85f);
		} else if (dryRmsBin >= std::min< std::size_t >(120U, rmsP10Bin + 4U)) {
			// In stationary HVAC/hum scenes the low RMS quantile is a useful
			// background anchor even when classic VAD misses a consonant. Preserve
			// frames at least 4 dB above that floor exactly delayed-dry.
			targetWetMix = 0.0f;
		}
	}
	// Protect a newly detected consonant immediately. Re-introduce stronger
	// background suppression over several frames after speech probability falls,
	// avoiding a hard spectral step at the word boundary.
	if (targetWetMix <= m_classicWetMix) {
		m_classicWetMix = targetWetMix;
	} else {
		m_classicWetMix = std::min(targetWetMix, m_classicWetMix + 0.15f);
	}

	for (unsigned int index = 0; index < frameSamples; ++index) {
		const float wet = static_cast< float >(processedSamples[index]) / 32768.0f;
		const float mixed = m_alignedDryFrame[index] + (m_classicWetMix * (wet - m_alignedDryFrame[index]));
		const float scaled = std::clamp(mixed * 32768.0f, -32768.0f, 32767.0f);
		processedSamples[index] = static_cast< std::int16_t >(std::lrint(scaled));
	}
	return true;
}

void Pipeline::recordClassicProcessingFrame(std::uint64_t durationNanoseconds) noexcept {
	if (!m_recipe || m_recipe->engine() != Engine::Speex || m_recipe->usesNeuralProcessor()
		|| m_fallbackActive) {
		return;
	}

	++m_processedFrames;
	m_lastProcessingNanoseconds = durationNanoseconds;
	recordProcessingDuration(durationNanoseconds);
	if (durationNanoseconds > m_frameDeadlineNanoseconds) {
		++m_deadlineMisses;
		failClosed(FallbackReason::DeadlineExceeded);
	}
}

void Pipeline::markClassicProcessingFailure(FallbackReason reason) noexcept {
	if (!m_recipe || m_recipe->engine() != Engine::Speex || m_recipe->usesNeuralProcessor()) {
		return;
	}
	failClosed(reason == FallbackReason::None ? FallbackReason::ProcessorUnavailable : reason);
}

bool Pipeline::restoreClassicAlignedDryFrame(std::int16_t *samples, unsigned int sampleCount) const noexcept {
	if (!samples || sampleCount != frameSamples || !m_recipe || m_recipe->engine() != Engine::Speex
		|| m_actualLatencySamples != frameSamples) {
		return false;
	}
	for (unsigned int index = 0; index < frameSamples; ++index) {
		const float scaled = std::clamp(m_alignedDryFrame[index] * 32768.0f, -32768.0f, 32767.0f);
		samples[index]     = static_cast< std::int16_t >(std::lrint(scaled));
	}
	return true;
}

bool Pipeline::fallbackActive() const noexcept {
	return m_fallbackActive;
}

FallbackReason Pipeline::fallbackReason() const noexcept {
	return m_fallbackReason;
}

bool Pipeline::alignedFallbackActive() const noexcept {
	return m_fallbackActive && m_recipe && m_recipe->effectiveProfile() != Profile::Original
		&& m_actualLatencySamples > 0;
}

unsigned int Pipeline::latencySamples() const noexcept {
	return m_actualLatencySamples;
}

std::uint64_t Pipeline::lastProcessingNanoseconds() const noexcept {
	return m_lastProcessingNanoseconds;
}

std::uint64_t Pipeline::lastWorkerProcessingNanoseconds() const noexcept {
	return m_processor ? m_processor->lastWorkerProcessingNanoseconds() : 0;
}

unsigned int Pipeline::workerPendingFrames() const noexcept {
	return m_processor ? m_processor->workerPendingFrames() : 0;
}

unsigned int Pipeline::workerSchedulingDelayFrames() const noexcept {
	return m_processor ? m_processor->workerSchedulingDelayFrames() : 0;
}

unsigned int Pipeline::workerSchedulingSlackFrames() const noexcept {
	return m_processor ? m_processor->workerSchedulingSlackFrames() : 0;
}

std::uint64_t Pipeline::frameDeadlineNanoseconds() const noexcept {
	return m_frameDeadlineNanoseconds;
}

Diagnostics Pipeline::diagnostics() const {
	const bool hasRecipe           = static_cast< bool >(m_recipe);
	const Profile requestedProfile = hasRecipe ? m_recipe->requestedProfile() : Profile::Original;
	const Profile activeProfile    = m_fallbackActive || !hasRecipe ? Profile::Original : m_recipe->effectiveProfile();
	const Engine activeEngine      = m_fallbackActive || !hasRecipe ? Engine::None : m_recipe->engine();
	const std::uint64_t workerProcessingFrames = m_processor ? m_processor->workerProcessingFrames() : 0;
	const std::uint64_t workerTotalProcessingNanoseconds =
		m_processor ? m_processor->workerTotalProcessingNanoseconds() : 0;
	const std::uint64_t workerMaximumProcessingNanoseconds =
		m_processor ? m_processor->workerMaximumProcessingNanoseconds() : 0;
	const std::uint64_t workerProcessingP99Nanoseconds =
		m_processor ? m_processor->workerProcessingP99Nanoseconds() : 0;
	const std::uint64_t processingP50Nanoseconds = processingPercentileNanoseconds(50);
	const std::uint64_t processingP95Nanoseconds = processingPercentileNanoseconds(95);
	const std::uint64_t processingP99Nanoseconds = processingPercentileNanoseconds(99);
	return Diagnostics(hasRecipe ? m_recipe->id() : QStringLiteral("input.original"),
					   hasRecipe ? m_recipe->revision() : RecipeCatalog::currentRevision, requestedProfile,
					   activeProfile, activeEngine, m_fallbackActive ? QString() : m_activeModelId,
					   m_fallbackActive ? QString() : m_activeModelSha256,
					   !hasRecipe ? originalLatencyBudgetSamples : m_recipe->latencyBudgetSamples(), latencySamples(),
					   m_processedFrames, m_neuralFrames, m_sanitizedInputSamples, m_clampedInputSamples,
					   m_clampedOutputSamples, m_deadlineMisses, m_fallbackCount, m_totalProcessingNanoseconds,
					   m_maximumProcessingNanoseconds, processingP50Nanoseconds, processingP95Nanoseconds,
					   processingP99Nanoseconds, workerProcessingFrames, workerTotalProcessingNanoseconds,
					   workerMaximumProcessingNanoseconds, workerProcessingP99Nanoseconds, m_fallbackActive,
					   m_fallbackReason);
}

void Pipeline::failClosed(FallbackReason reason) noexcept {
	if (m_fallbackActive) {
		return;
	}
	m_fallbackActive = true;
	m_fallbackReason = reason;
	++m_fallbackCount;
}

void Pipeline::recordProcessingDuration(std::uint64_t durationNanoseconds) noexcept {
	++m_timedProcessingFrames;
	m_totalProcessingNanoseconds += durationNanoseconds;
	m_maximumProcessingNanoseconds  = std::max(m_maximumProcessingNanoseconds, durationNanoseconds);
	const std::uint64_t quotient    = durationNanoseconds / processingHistogramBucketNanoseconds;
	const std::uint64_t remainder   = durationNanoseconds % processingHistogramBucketNanoseconds;
	const std::uint64_t bucketIndex = std::min< std::uint64_t >(
		quotient + (remainder == 0 ? 0 : 1), static_cast< std::uint64_t >(m_processingHistogram.size() - 1));
	const std::size_t bucket = static_cast< std::size_t >(bucketIndex);
	++m_processingHistogram[bucket];
}

std::uint64_t Pipeline::processingPercentileNanoseconds(unsigned int percentile) const noexcept {
	if (m_timedProcessingFrames == 0 || percentile == 0 || percentile > 100) {
		return 0;
	}
	const std::uint64_t targetRank = (m_timedProcessingFrames * percentile + 99) / 100;
	std::uint64_t accumulated      = 0;
	for (std::size_t bucket = 0; bucket < m_processingHistogram.size(); ++bucket) {
		accumulated += m_processingHistogram[bucket];
		if (accumulated >= targetRank) {
			if (bucket + 1 == m_processingHistogram.size()) {
				return m_maximumProcessingNanoseconds;
			}
			return static_cast< std::uint64_t >(bucket) * processingHistogramBucketNanoseconds;
		}
	}
	return m_maximumProcessingNanoseconds;
}

void Pipeline::resetCounters() noexcept {
	m_processedFrames              = 0;
	m_neuralFrames                 = 0;
	m_timedProcessingFrames        = 0;
	m_sanitizedInputSamples        = 0;
	m_clampedInputSamples          = 0;
	m_clampedOutputSamples         = 0;
	m_deadlineMisses               = 0;
	m_fallbackCount                = 0;
	m_totalProcessingNanoseconds   = 0;
	m_maximumProcessingNanoseconds = 0;
	m_lastProcessingNanoseconds    = 0;
	m_processingHistogram.fill(0);
}

} // namespace Mumble::InputEnhancement
