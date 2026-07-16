// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementAutoV2.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSysInfo>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace Mumble::InputEnhancement::AutoV2 {
namespace {
	constexpr std::array< std::uint64_t, callbackHistogramBucketCount - 1U > callbackBucketBounds = {
		1'000'000, 2'000'000, 3'000'000, 5'000'000, 8'000'000, 10'000'000
	};

	void appendU32(QByteArray &bytes, const std::uint32_t value) {
		const char encoded[] = {
			static_cast< char >((value >> 24U) & 0xffU),
			static_cast< char >((value >> 16U) & 0xffU),
			static_cast< char >((value >> 8U) & 0xffU),
			static_cast< char >(value & 0xffU),
		};
		bytes.append(encoded, static_cast< qsizetype >(sizeof(encoded)));
	}

	void appendText(QByteArray &bytes, const QString &value) {
		const QByteArray utf8 = value.toUtf8();
		appendU32(bytes, static_cast< std::uint32_t >(utf8.size()));
		bytes.append(utf8);
	}

	bool candidateLess(const AutoRecipeCandidateBinding &left, const AutoRecipeCandidateBinding &right) noexcept {
		return static_cast< std::uint8_t >(left.profile) < static_cast< std::uint8_t >(right.profile);
	}

	bool safeIdentifier(const QString &value, const int maximumLength) {
		if (value.isEmpty() || value.size() > maximumLength) {
			return false;
		}
		return std::all_of(value.cbegin(), value.cend(), [](const QChar character) {
			return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
				   || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
				   || (character >= QLatin1Char('a') && character <= QLatin1Char('z')) || character == QLatin1Char('.')
				   || character == QLatin1Char('_') || character == QLatin1Char(':') || character == QLatin1Char('-');
		});
	}

	bool validCpuTier(const CpuClass cpuTier) noexcept {
		switch (cpuTier) {
			case CpuClass::Low:
			case CpuClass::Standard:
			case CpuClass::High:
				return true;
		}
		return false;
	}

	bool allowedCandidate(const Profile profile) noexcept {
		return profile == Profile::Light || profile == Profile::Balanced || profile == Profile::Quality;
	}

	QString sha256File(const QString &path) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return {};
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while (!file.atEnd()) {
			const QByteArray chunk = file.read(1024 * 1024);
			if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
				return {};
			}
			hash.addData(chunk);
		}
		return QString::fromLatin1(hash.result().toHex());
	}

	QString currentCpuFingerprint() {
		QByteArray identity;
		appendText(identity, QSysInfo::currentCpuArchitecture());
		appendText(identity, QSysInfo::buildCpuArchitecture());
		appendText(identity, QString::fromLocal8Bit(qgetenv("PROCESSOR_IDENTIFIER")));
		appendText(identity, QString::fromLocal8Bit(qgetenv("PROCESSOR_ARCHITECTURE")));
		appendText(identity, QString::fromLocal8Bit(qgetenv("PROCESSOR_REVISION")));
		appendText(identity, QString::fromLocal8Bit(qgetenv("NUMBER_OF_PROCESSORS")));
		return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
	}

	std::size_t profileIndex(const Profile profile) noexcept {
		const std::size_t index = static_cast< std::size_t >(profile);
		return index < trackedProfileCount ? index : 0U;
	}

	std::size_t fallbackIndex(const FallbackReason reason) noexcept {
		const std::size_t index = static_cast< std::size_t >(reason);
		return index < fallbackReasonCount ? index : 0U;
	}

	template< typename T > void saturatingIncrement(T &value) noexcept {
		if (value < std::numeric_limits< T >::max()) {
			++value;
		}
	}

	template< typename T > void saturatingAdd(T &value, const T increment) noexcept {
		value =
			increment > std::numeric_limits< T >::max() - value ? std::numeric_limits< T >::max() : value + increment;
	}
} // namespace

bool CapabilityProbeKey::operator==(const CapabilityProbeKey &other) const {
	return buildSha256 == other.buildSha256 && cpuSha256 == other.cpuSha256 && modelSetSha256 == other.modelSetSha256;
}

CapabilityProbeKey currentCapabilityProbeKey(const QString &modelSetSha256) {
	// Hashing the executable is also cached because every subsequent key change
	// in this process can only come from the verified model/recipe payload.
	static const QString buildSha256 = sha256File(QCoreApplication::applicationFilePath());
	static const QString cpuSha256   = currentCpuFingerprint();
	return { buildSha256, cpuSha256, modelSetSha256 };
}

QString capabilityProbeBindingFingerprint(const CapabilityProbeKey &key) {
	if (!isValidAutoRecipeSetFingerprint(key.buildSha256) || !isValidAutoRecipeSetFingerprint(key.cpuSha256)
		|| !isValidAutoRecipeSetFingerprint(key.modelSetSha256)) {
		return {};
	}
	QByteArray canonical("mumble-input-enhancement-capability-probe", 41);
	canonical.append('\0');
	appendU32(canonical, capabilityProbeSchemaVersion);
	appendText(canonical, key.buildSha256);
	appendText(canonical, key.cpuSha256);
	appendText(canonical, key.modelSetSha256);
	return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

CpuClass classifyCapabilityProbeDuration(const std::uint64_t elapsedNanoseconds) noexcept {
	if (elapsedNanoseconds > 0 && elapsedNanoseconds <= 2'500'000) {
		return CpuClass::High;
	}
	if (elapsedNanoseconds <= 7'500'000) {
		return CpuClass::Standard;
	}
	return CpuClass::Low;
}

CapabilityProbeResult runCapabilityProbe(const CapabilityProbeKey &key) {
	CapabilityProbeResult result;
	result.key                = key;
	result.bindingFingerprint = capabilityProbeBindingFingerprint(key);
	if (result.bindingFingerprint.isEmpty()) {
		return result;
	}

	// A fixed DSP-shaped workload replaces the thread-count proxy. It is short
	// enough for startup/control-thread use, uses no model processor state, and
	// cannot migrate into the callback because this API owns its timer and heap-
	// backed Qt result. The exact packaged model set remains part of the cache
	// key, so a model update requires a new measurement.
	std::array< float, 256 > state = {};
	for (std::size_t index = 0; index < state.size(); ++index) {
		state[index] = static_cast< float >((index % 31U) + 1U) / 32.0f;
	}
	QElapsedTimer timer;
	timer.start();
	float checksum = 0.0f;
	for (std::size_t round = 0; round < 4096U; ++round) {
		for (std::size_t index = 0; index < state.size(); ++index) {
			const float neighbor = state[(index + 1U) % state.size()];
			const float updated  = state[index] * 0.9765625f + neighbor * 0.01953125f + 0.0009765625f;
			state[index]         = updated;
			checksum += updated * 0.000001f;
		}
	}
	const qint64 elapsed      = timer.nsecsElapsed();
	result.elapsedNanoseconds = elapsed > 0 ? static_cast< std::uint64_t >(elapsed) : 1U;
	if (!std::isfinite(checksum) || result.elapsedNanoseconds > capabilityProbeMaximumNanoseconds) {
		result.cpuTier = CpuClass::Low;
		return result;
	}
	result.cpuTier = classifyCapabilityProbeDuration(result.elapsedNanoseconds);
	result.valid   = true;
	return result;
}

CapabilityProbeResult cachedCapabilityProbe(const CapabilityProbeKey &key) {
	static QMutex cacheMutex;
	static std::optional< CapabilityProbeResult > cached;
	QMutexLocker lock(&cacheMutex);
	if (!cached || !(cached->key == key)) {
		cached = runCapabilityProbe(key);
	}
	return *cached;
}

bool AutoRecipeCandidateBinding::operator==(const AutoRecipeCandidateBinding &other) const {
	return profile == other.profile && recipe == other.recipe;
}

bool AutoRecipeSetBinding::operator==(const AutoRecipeSetBinding &other) const {
	return catalogRevision == other.catalogRevision && policyVersion == other.policyVersion
		   && mixVersion == other.mixVersion && cpuTier == other.cpuTier && candidates == other.candidates
		   && setFingerprint == other.setFingerprint;
}

AutoRecipeSetBinding
	makeAutoRecipeSetBinding(QString catalogRevision, const std::uint32_t policyVersion, const std::uint32_t mixVersion,
							 const CpuClass cpuTier,
							 std::array< AutoRecipeCandidateBinding, requiredCandidateCount > candidates) {
	std::sort(candidates.begin(), candidates.end(), candidateLess);
	AutoRecipeSetBinding binding;
	binding.catalogRevision = std::move(catalogRevision);
	binding.policyVersion   = policyVersion;
	binding.mixVersion      = mixVersion;
	binding.cpuTier         = cpuTier;
	binding.candidates      = std::move(candidates);
	binding.setFingerprint  = autoRecipeSetFingerprint(binding);
	return binding;
}

QString autoRecipeSetFingerprint(const AutoRecipeSetBinding &binding) {
	std::array< AutoRecipeCandidateBinding, requiredCandidateCount > candidates = binding.candidates;
	std::sort(candidates.begin(), candidates.end(), candidateLess);

	QByteArray canonical("mumble-input-enhancement-auto-recipe-set", 40);
	canonical.append('\0');
	appendU32(canonical, recipeSetSchemaVersion);
	appendText(canonical, binding.catalogRevision);
	appendU32(canonical, binding.policyVersion);
	appendU32(canonical, binding.mixVersion);
	appendU32(canonical, static_cast< std::uint32_t >(binding.cpuTier));
	appendU32(canonical, static_cast< std::uint32_t >(candidates.size()));

	for (const AutoRecipeCandidateBinding &candidate : candidates) {
		const RecipeBinding &recipe = candidate.recipe;
		appendU32(canonical, static_cast< std::uint32_t >(candidate.profile));
		appendText(canonical, recipe.catalogRevision);
		appendText(canonical, recipe.recipeId);
		appendU32(canonical, recipe.recipeRevision);
		appendU32(canonical, static_cast< std::uint32_t >(recipe.requestedProfile));
		appendU32(canonical, static_cast< std::uint32_t >(recipe.effectiveProfile));
		appendU32(canonical, static_cast< std::uint32_t >(recipe.engine));
		appendText(canonical, recipe.modelId);
		appendText(canonical, recipe.modelSha256);
		appendText(canonical, recipe.modelRelativePath);
		appendText(canonical, recipe.executionFingerprint);
		appendU32(canonical, static_cast< std::uint32_t >(recipe.noiseReduction));
		appendU32(canonical, static_cast< std::uint32_t >(recipe.naturalCrisp));
		appendU32(canonical, recipe.latencyBudgetSamples);
		appendU32(canonical, static_cast< std::uint32_t >(recipe.minimumCpuClass));
	}

	return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

RecipeSetValidationResult validateAutoRecipeSetBinding(const AutoRecipeSetBinding &binding,
													   const QString &expectedCatalogRevision,
													   const std::uint32_t expectedPolicyVersion,
													   const std::uint32_t expectedMixVersion,
													   const CpuClass expectedCpuTier) {
	auto failure = [](const RecipeSetValidationError error) {
		RecipeSetValidationResult result;
		result.error       = error;
		result.safeProfile = Profile::Original;
		return result;
	};

	if (!safeIdentifier(binding.catalogRevision, 64)) {
		return failure(RecipeSetValidationError::InvalidCatalogRevision);
	}
	if (binding.policyVersion == 0) {
		return failure(RecipeSetValidationError::InvalidPolicyVersion);
	}
	if (binding.mixVersion == 0) {
		return failure(RecipeSetValidationError::InvalidMixVersion);
	}
	if (!validCpuTier(binding.cpuTier)) {
		return failure(RecipeSetValidationError::InvalidCpuTier);
	}

	std::array< bool, requiredCandidateCount > seen = {};
	for (std::size_t index = 0; index < binding.candidates.size(); ++index) {
		const AutoRecipeCandidateBinding &candidate = binding.candidates[index];
		if (!allowedCandidate(candidate.profile)) {
			return failure(RecipeSetValidationError::ForbiddenCandidate);
		}
		if (candidate.recipe.requestedProfile != Profile::Auto
			|| candidate.recipe.effectiveProfile != candidate.profile) {
			return failure(RecipeSetValidationError::InvalidCandidateBinding);
		}
		// RecipeBinding's settings validator intentionally rejects Auto because
		// fixed-profile probation cannot persist an Auto recipe. Validate the
		// same structural/package invariants against the concrete set member
		// while retaining and hashing the actual input.auto.* identity above.
		RecipeBinding structurallyConcrete    = candidate.recipe;
		structurallyConcrete.requestedProfile = candidate.profile;
		if (!isValidRecipeBinding(structurallyConcrete)) {
			return failure(RecipeSetValidationError::InvalidCandidateBinding);
		}
		if (candidate.recipe.catalogRevision != binding.catalogRevision) {
			return failure(RecipeSetValidationError::CandidateCatalogMismatch);
		}

		std::size_t member = requiredCandidateCount;
		if (candidate.profile == Profile::Light) {
			member = 0;
		} else if (candidate.profile == Profile::Balanced) {
			member = 1;
		} else if (candidate.profile == Profile::Quality) {
			member = 2;
		}
		if (member >= requiredCandidateCount) {
			return failure(RecipeSetValidationError::ForbiddenCandidate);
		}
		if (seen[member]) {
			return failure(RecipeSetValidationError::DuplicateCandidate);
		}
		seen[member] = true;
		if (member != index) {
			return failure(RecipeSetValidationError::NonCanonicalOrder);
		}
	}
	if (!std::all_of(seen.cbegin(), seen.cend(), [](const bool value) { return value; })) {
		return failure(RecipeSetValidationError::MissingCandidate);
	}
	if (binding.setFingerprint.size() != 64 || binding.setFingerprint != autoRecipeSetFingerprint(binding)) {
		return failure(RecipeSetValidationError::FingerprintMismatch);
	}
	if (binding.catalogRevision != expectedCatalogRevision) {
		return failure(RecipeSetValidationError::CatalogDrift);
	}
	if (binding.policyVersion != expectedPolicyVersion) {
		return failure(RecipeSetValidationError::PolicyDrift);
	}
	if (binding.mixVersion != expectedMixVersion) {
		return failure(RecipeSetValidationError::MixDrift);
	}
	if (binding.cpuTier != expectedCpuTier) {
		return failure(RecipeSetValidationError::CpuTierDrift);
	}

	return {};
}

void SessionDiagnostics::reset() noexcept {
	m_snapshot = {};
}

void SessionDiagnostics::recordDecision(const Profile profile) noexcept {
	saturatingIncrement(m_snapshot.decisions);
	saturatingIncrement(m_snapshot.decisionsByProfile[profileIndex(profile)]);
	m_snapshot.lastDecisionProfile = profile;
}

void SessionDiagnostics::recordCallbackFrame(const Profile residentProfile,
											 const std::uint64_t durationNanoseconds) noexcept {
	saturatingIncrement(m_snapshot.profileResidencyFrames[profileIndex(residentProfile)]);
	saturatingIncrement(m_snapshot.callbackFrames);
	saturatingAdd(m_snapshot.callbackTotalNanoseconds, durationNanoseconds);
	m_snapshot.callbackMaximumNanoseconds = std::max(m_snapshot.callbackMaximumNanoseconds, durationNanoseconds);

	std::size_t bucket = 0;
	while (bucket < callbackBucketBounds.size() && durationNanoseconds > callbackBucketBounds[bucket]) {
		++bucket;
	}
	saturatingIncrement(m_snapshot.callbackHistogram[bucket]);
}

void SessionDiagnostics::recordTransitionStarted(const Profile source, const Profile candidate) noexcept {
	saturatingIncrement(m_snapshot.transitionsStarted);
	m_snapshot.lastTransitionSource    = source;
	m_snapshot.lastTransitionCandidate = candidate;
	m_snapshot.transitionState         = TransitionState::Priming;
	m_snapshot.lastAbort               = TransitionAbortReason::None;
}

void SessionDiagnostics::recordTransitionState(const TransitionState state) noexcept {
	m_snapshot.transitionState = state;
}

void SessionDiagnostics::recordTransitionCompleted() noexcept {
	saturatingIncrement(m_snapshot.transitionsCompleted);
	m_snapshot.transitionState = TransitionState::Active;
}

void SessionDiagnostics::recordTransitionAbort(const TransitionAbortReason reason) noexcept {
	const std::size_t reasonIndex = static_cast< std::size_t >(reason);
	if (reason == TransitionAbortReason::None || reasonIndex >= transitionAbortReasonCount) {
		return;
	}
	saturatingIncrement(m_snapshot.transitionsAborted);
	saturatingIncrement(m_snapshot.transitionAbortsByReason[reasonIndex]);
	m_snapshot.transitionState = TransitionState::Abort;
	m_snapshot.lastAbort       = reason;
}

void SessionDiagnostics::recordQuarantine(const Profile profile) noexcept {
	saturatingIncrement(m_snapshot.quarantineEvents);
	saturatingIncrement(m_snapshot.quarantineByProfile[profileIndex(profile)]);
}

void SessionDiagnostics::recordFallback(const FallbackReason reason) noexcept {
	if (reason == FallbackReason::None) {
		return;
	}
	saturatingIncrement(m_snapshot.fallbackEvents);
	saturatingIncrement(m_snapshot.fallbackByReason[fallbackIndex(reason)]);
}

SessionDiagnosticsSnapshot SessionDiagnostics::snapshot() const noexcept {
	return m_snapshot;
}

std::uint64_t SessionDiagnostics::callbackBucketUpperBoundNanoseconds(const std::size_t bucket) noexcept {
	return bucket < callbackBucketBounds.size() ? callbackBucketBounds[bucket]
												: std::numeric_limits< std::uint64_t >::max();
}

TransitionCoordinator::TransitionCoordinator(SessionDiagnostics *diagnostics) noexcept
	: m_diagnostics(diagnostics), m_rotationCos(static_cast< float >(
									  std::cos(1.57079632679489661923 / static_cast< double >(crossfadeSamples - 1U)))),
	  m_rotationSin(
		  static_cast< float >(std::sin(1.57079632679489661923 / static_cast< double >(crossfadeSamples - 1U)))) {
}

void TransitionCoordinator::setDiagnostics(SessionDiagnostics *diagnostics) noexcept {
	m_diagnostics = diagnostics;
}

void TransitionCoordinator::reset() noexcept {
	m_sourceDelay.reset(0);
	m_candidateDelay.reset(0);
	m_alignedSource.fill(0.0f);
	m_alignedCandidate.fill(0.0f);
	m_sourceProfile    = Profile::Original;
	m_candidateProfile = Profile::Original;
	m_state            = TransitionState::Idle;
	m_abortReason      = TransitionAbortReason::None;
	m_fadedSamples     = 0;
	m_sourceGain       = 1.0f;
	m_candidateGain    = 0.0f;
	if (m_diagnostics) {
		m_diagnostics->recordTransitionState(TransitionState::Idle);
	}
}

bool TransitionCoordinator::begin(const Profile sourceProfile, const unsigned int sourceLatencySamples,
								  const Profile candidateProfile, const unsigned int candidateLatencySamples) noexcept {
	m_sourceProfile    = sourceProfile;
	m_candidateProfile = candidateProfile;
	m_abortReason      = TransitionAbortReason::None;
	m_fadedSamples     = 0;
	m_sourceGain       = 1.0f;
	m_candidateGain    = 0.0f;

	if (!isAutoCandidate(sourceProfile) || !isAutoCandidate(candidateProfile) || sourceProfile == candidateProfile
		|| sourceLatencySamples > maximumTransitionLatencySamples
		|| candidateLatencySamples > maximumTransitionLatencySamples) {
		m_state       = TransitionState::Abort;
		m_abortReason = TransitionAbortReason::InvalidConfiguration;
		if (m_diagnostics) {
			m_diagnostics->recordTransitionAbort(m_abortReason);
		}
		return false;
	}

	const unsigned int alignmentLatency = std::max(sourceLatencySamples, candidateLatencySamples);
	m_sourceDelay.reset(alignmentLatency - sourceLatencySamples);
	m_candidateDelay.reset(alignmentLatency - candidateLatencySamples);
	m_alignedSource.fill(0.0f);
	m_alignedCandidate.fill(0.0f);
	m_state = TransitionState::Priming;
	if (m_diagnostics) {
		m_diagnostics->recordTransitionStarted(sourceProfile, candidateProfile);
	}
	return true;
}

TransitionFrameResult TransitionCoordinator::processDualPipelineFrame(const Frame &sourceFrame,
																	  const Frame &candidateFrame,
																	  const TransitionFrameHealth &health,
																	  Frame &outputFrame) noexcept {
	if (m_state == TransitionState::Idle) {
		copyFiniteOrSilence(sourceFrame, outputFrame);
		return result();
	}
	if (m_state == TransitionState::Abort) {
		copyFiniteOrSilence(sourceFrame, outputFrame);
		return result();
	}
	if (m_state == TransitionState::Active) {
		copyFiniteOrSilence(candidateFrame, outputFrame);
		return result();
	}

	if (!frameIsFinite(sourceFrame)) {
		return abortTransition(TransitionAbortReason::SourceInvalidOutput, sourceFrame, outputFrame);
	}
	if (!health.candidateAvailable) {
		return abortTransition(TransitionAbortReason::CandidateUnavailable, sourceFrame, outputFrame);
	}
	if (!health.candidateDeadlineMet) {
		return abortTransition(TransitionAbortReason::CandidateDeadlineMiss, sourceFrame, outputFrame);
	}
	if (!health.candidateSucceeded || !frameIsFinite(candidateFrame)) {
		return abortTransition(TransitionAbortReason::CandidateInvalidOutput, sourceFrame, outputFrame);
	}

	const bool verifiedSilence =
		!health.acousticSpeech && !health.tailDrainActive
		&& health.verifiedSilentFrames >= AutoV1::AcousticSilenceSwitchBoundary::minimumSilentFrames;

	if (m_state == TransitionState::Rebase) {
		if (!verifiedSilence) {
			return abortTransition(health.tailDrainActive ? TransitionAbortReason::TailDrainResumed
														  : TransitionAbortReason::SilenceLost,
								   sourceFrame, outputFrame);
		}
		outputFrame = candidateFrame;
		m_state     = TransitionState::Active;
		if (m_diagnostics) {
			m_diagnostics->recordTransitionCompleted();
		}
		return result(true, false);
	}

	m_sourceDelay.process(sourceFrame, m_alignedSource);
	m_candidateDelay.process(candidateFrame, m_alignedCandidate);

	if (m_state == TransitionState::Priming) {
		outputFrame = sourceFrame;
		if (verifiedSilence && m_sourceDelay.primed() && m_candidateDelay.primed()) {
			m_state = TransitionState::Fading;
			if (m_diagnostics) {
				m_diagnostics->recordTransitionState(m_state);
			}
		}
		return result();
	}

	if (!verifiedSilence) {
		return abortTransition(health.tailDrainActive ? TransitionAbortReason::TailDrainResumed
													  : TransitionAbortReason::SilenceLost,
							   sourceFrame, outputFrame);
	}

	for (std::size_t sample = 0; sample < frameSamples; ++sample) {
		if (m_fadedSamples + 1U == crossfadeSamples) {
			m_sourceGain    = 0.0f;
			m_candidateGain = 1.0f;
		}
		outputFrame[sample] = m_alignedSource[sample] * m_sourceGain + m_alignedCandidate[sample] * m_candidateGain;

		if (m_fadedSamples + 1U < crossfadeSamples) {
			const float nextSource    = m_sourceGain * m_rotationCos - m_candidateGain * m_rotationSin;
			const float nextCandidate = m_candidateGain * m_rotationCos + m_sourceGain * m_rotationSin;
			m_sourceGain              = nextSource;
			m_candidateGain           = nextCandidate;
		}
		++m_fadedSamples;
	}

	if (m_fadedSamples >= crossfadeSamples) {
		m_state = TransitionState::Rebase;
		if (m_diagnostics) {
			m_diagnostics->recordTransitionState(m_state);
		}
	}
	return result();
}

TransitionState TransitionCoordinator::state() const noexcept {
	return m_state;
}

TransitionAbortReason TransitionCoordinator::abortReason() const noexcept {
	return m_abortReason;
}

Profile TransitionCoordinator::sourceProfile() const noexcept {
	return m_sourceProfile;
}

Profile TransitionCoordinator::candidateProfile() const noexcept {
	return m_candidateProfile;
}

bool TransitionCoordinator::dualPipelineRequired() const noexcept {
	return m_state == TransitionState::Priming || m_state == TransitionState::Fading
		   || m_state == TransitionState::Rebase;
}

std::size_t TransitionCoordinator::fadedSamples() const noexcept {
	return m_fadedSamples;
}

void TransitionCoordinator::FixedDelayLine::reset(const unsigned int delaySamples) noexcept {
	m_samples.fill(0.0f);
	m_delaySamples   = std::min(delaySamples, maximumTransitionLatencySamples);
	m_position       = 0;
	m_samplesWritten = 0;
}

void TransitionCoordinator::FixedDelayLine::process(const Frame &input, Frame &output) noexcept {
	if (m_delaySamples == 0) {
		output = input;
		return;
	}

	for (std::size_t sample = 0; sample < input.size(); ++sample) {
		output[sample]        = m_samples[m_position];
		m_samples[m_position] = input[sample];
		++m_position;
		if (m_position == m_delaySamples) {
			m_position = 0;
		}
		if (m_samplesWritten < m_delaySamples) {
			++m_samplesWritten;
		}
	}
}

bool TransitionCoordinator::FixedDelayLine::primed() const noexcept {
	return m_samplesWritten >= m_delaySamples;
}

bool TransitionCoordinator::isAutoCandidate(const Profile profile) noexcept {
	return allowedCandidate(profile);
}

bool TransitionCoordinator::frameIsFinite(const Frame &frame) noexcept {
	return std::all_of(frame.cbegin(), frame.cend(), [](const float sample) { return std::isfinite(sample); });
}

void TransitionCoordinator::copyFiniteOrSilence(const Frame &frame, Frame &output) noexcept {
	if (frameIsFinite(frame)) {
		output = frame;
	} else {
		output.fill(0.0f);
	}
}

TransitionFrameResult TransitionCoordinator::result(const bool completedThisFrame,
													const bool abortedThisFrame) const noexcept {
	TransitionFrameResult frameResult;
	frameResult.state                = m_state;
	frameResult.abortReason          = m_abortReason;
	frameResult.outputProfile        = (m_state == TransitionState::Active || m_state == TransitionState::Rebase)
										   ? m_candidateProfile
										   : m_sourceProfile;
	frameResult.dualPipelineRequired = dualPipelineRequired();
	frameResult.completedThisFrame   = completedThisFrame;
	frameResult.abortedThisFrame     = abortedThisFrame;
	return frameResult;
}

TransitionFrameResult TransitionCoordinator::abortTransition(const TransitionAbortReason reason,
															 const Frame &sourceFrame, Frame &outputFrame) noexcept {
	m_state       = TransitionState::Abort;
	m_abortReason = reason;
	copyFiniteOrSilence(sourceFrame, outputFrame);
	if (m_diagnostics) {
		m_diagnostics->recordTransitionAbort(reason);
		if (reason == TransitionAbortReason::CandidateUnavailable
			|| reason == TransitionAbortReason::CandidateInvalidOutput
			|| reason == TransitionAbortReason::CandidateDeadlineMiss) {
			m_diagnostics->recordQuarantine(m_candidateProfile);
		}
		recordFallbackForAbort(reason);
	}
	return result(false, true);
}

void TransitionCoordinator::recordFallbackForAbort(const TransitionAbortReason reason) noexcept {
	if (!m_diagnostics) {
		return;
	}
	switch (reason) {
		case TransitionAbortReason::CandidateUnavailable:
			m_diagnostics->recordFallback(FallbackReason::ProcessorNotReady);
			break;
		case TransitionAbortReason::CandidateInvalidOutput:
			m_diagnostics->recordFallback(FallbackReason::InvalidOutput);
			break;
		case TransitionAbortReason::CandidateDeadlineMiss:
			m_diagnostics->recordFallback(FallbackReason::DeadlineExceeded);
			break;
		case TransitionAbortReason::None:
		case TransitionAbortReason::InvalidConfiguration:
		case TransitionAbortReason::SourceInvalidOutput:
		case TransitionAbortReason::SilenceLost:
		case TransitionAbortReason::TailDrainResumed:
		case TransitionAbortReason::Count:
			break;
	}
}

} // namespace Mumble::InputEnhancement::AutoV2
