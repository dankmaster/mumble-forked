// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SpeechCleanupTestAudio.h"

#include "ClientUser.h"
#include "Global.h"
#include "InputEnhancementPackageVerifier.h"
#include "InputEnhancementPolicyController.h"
#include "ServerHandler.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtCore/QtEndian>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>

namespace {

constexpr auto TEST_FRAME_DURATION      = std::chrono::milliseconds(10);
constexpr unsigned int TEST_SAMPLE_RATE = SAMPLE_RATE;
constexpr unsigned int TEST_FRAME_SIZE  = TEST_SAMPLE_RATE / 100;

const QString ENV_ENABLE         = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_ENABLE");
const QString ENV_TOKEN          = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_TOKEN");
const QString ENV_INPUT_WAV      = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_WAV");
const QString ENV_START_GATE     = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_START_GATE");
const QString ENV_INPUT_DONE     = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_DONE_PATH");
const QString ENV_INPUT_TAIL     = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_TAIL_FRAMES");
const QString ENV_INPUT_PREROLL  = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_PREROLL_FRAMES");
const QString ENV_HOLD_PTT       = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_HOLD_PTT");
const QString ENV_VOICE_CONTRACT = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_VOICE_CONTRACT");
const QString ENV_PRE_OPUS_WAV   = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_PRE_OPUS_WAV");
const QString ENV_CAPTURE_WAV    = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_CAPTURE_WAV");
const QString ENV_CAPTURE_SENDER = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_CAPTURE_SENDER");
const QString ENV_STOP_GATE      = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_STOP_GATE");
const QString ENV_CAPTURE_DONE   = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_DONE_PATH");

QString environmentValue(const QString &name) {
	return qEnvironmentVariable(name.toUtf8().constData()).trimmed();
}

bool runtimeEnabled() {
	return environmentValue(ENV_ENABLE) == QLatin1String("1") && !environmentValue(ENV_TOKEN).isEmpty();
}

bool gateMatchesRunToken(const QString &path) {
	if (path.isEmpty()) {
		return false;
	}

	QFile gate(path);
	if (!gate.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	return QString::fromUtf8(gate.readAll()).trimmed() == environmentValue(ENV_TOKEN);
}

bool writeJsonAtomically(const QString &path, const QJsonObject &object, QString *errorMessage = nullptr) {
	if (path.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("No completion path was configured");
		}
		return false;
	}

	const QFileInfo fileInfo(path);
	if (!QDir().mkpath(fileInfo.absolutePath())) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not create completion directory '%1'").arg(fileInfo.absolutePath());
		}
		return false;
	}

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (errorMessage) {
			*errorMessage = file.errorString();
		}
		return false;
	}

	const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Indented);
	if (file.write(encoded) != encoded.size() || !file.commit()) {
		if (errorMessage) {
			*errorMessage = file.errorString();
		}
		return false;
	}

	return true;
}

SNDFILE *openSoundFile(const QString &path, int mode, SF_INFO *info) {
#ifdef Q_OS_WIN
	return sf_wchar_open(path.toStdWString().c_str(), mode, info);
#else
	return sf_open(path.toUtf8().constData(), mode, info);
#endif
}

void paceRealtime(std::chrono::steady_clock::time_point &nextDeadline) {
	nextDeadline += TEST_FRAME_DURATION;
	const auto now = std::chrono::steady_clock::now();
	if (now < nextDeadline) {
		std::this_thread::sleep_until(nextDeadline);
	} else if (now - nextDeadline > std::chrono::milliseconds(50)) {
		// Do not submit a large burst after a debugger pause or heavily delayed frame.
		nextDeadline = now;
	}
}

int configuredTailFrames() {
	bool ok       = false;
	const int raw = environmentValue(ENV_INPUT_TAIL).toInt(&ok);
	return ok ? std::clamp(raw, 0, 500) : 0;
}

int configuredPreRollFrames() {
	bool ok       = false;
	const int raw = environmentValue(ENV_INPUT_PREROLL).toInt(&ok);
	return ok ? std::clamp(raw, 0, 500) : 0;
}

QString noiseCancelModeName(Settings::NoiseCancel mode) {
	switch (mode) {
		case Settings::NoiseCancelOff:
			return QStringLiteral("Off");
		case Settings::NoiseCancelSpeex:
			return QStringLiteral("Speex");
		case Settings::NoiseCancelRNN:
			return QStringLiteral("RNN");
		case Settings::NoiseCancelBoth:
			return QStringLiteral("Speex&RNN");
	}
	return QStringLiteral("Unknown");
}

QString transmitModeName(Settings::AudioTransmit mode) {
	switch (mode) {
		case Settings::Continuous:
			return QStringLiteral("continuous");
		case Settings::PushToTalk:
			return QStringLiteral("push_to_talk");
		case Settings::VAD:
			return QStringLiteral("vad");
	}
	return QStringLiteral("unknown");
}

QString inputEnhancementProfileName(Mumble::InputEnhancement::Profile profile) {
	switch (profile) {
		case Mumble::InputEnhancement::Profile::Original:
			return QStringLiteral("Original");
		case Mumble::InputEnhancement::Profile::Light:
			return QStringLiteral("Light");
		case Mumble::InputEnhancement::Profile::Balanced:
			return QStringLiteral("Balanced");
		case Mumble::InputEnhancement::Profile::Quality:
			return QStringLiteral("Quality");
		case Mumble::InputEnhancement::Profile::Auto:
			return QStringLiteral("Auto");
		case Mumble::InputEnhancement::Profile::VoiceFocus:
			return QStringLiteral("VoiceFocus");
	}
	return QStringLiteral("Unknown");
}

class ScopedE2EPushToTalk final {
public:
	explicit ScopedE2EPushToTalk(bool enabled) : m_active(enabled) {
		if (m_active) {
			++Global::get().iPushToTalk;
		}
	}

	~ScopedE2EPushToTalk() { release(); }

	void release() noexcept {
		if (!m_active) {
			return;
		}
		if (Global::get().iPushToTalk > 0) {
			--Global::get().iPushToTalk;
		}
		m_active = false;
	}

	bool active() const noexcept { return m_active; }

private:
	bool m_active;
};

QString inputEnhancementFallbackReasonName(Mumble::InputEnhancement::FallbackReason reason) {
	switch (reason) {
		case Mumble::InputEnhancement::FallbackReason::None:
			return QStringLiteral("none");
		case Mumble::InputEnhancement::FallbackReason::ProcessorUnavailable:
			return QStringLiteral("processor_unavailable");
		case Mumble::InputEnhancement::FallbackReason::ProcessorNotReady:
			return QStringLiteral("processor_not_ready");
		case Mumble::InputEnhancement::FallbackReason::ProcessorFallback:
			return QStringLiteral("processor_fallback");
		case Mumble::InputEnhancement::FallbackReason::UnexpectedModel:
			return QStringLiteral("unexpected_model");
		case Mumble::InputEnhancement::FallbackReason::LatencyBudgetExceeded:
			return QStringLiteral("latency_budget_exceeded");
		case Mumble::InputEnhancement::FallbackReason::InvalidFrame:
			return QStringLiteral("invalid_frame");
		case Mumble::InputEnhancement::FallbackReason::InvalidOutput:
			return QStringLiteral("invalid_output");
		case Mumble::InputEnhancement::FallbackReason::DeadlineExceeded:
			return QStringLiteral("deadline_exceeded");
		case Mumble::InputEnhancement::FallbackReason::ProcessorException:
			return QStringLiteral("processor_exception");
	}
	return QStringLiteral("unknown");
}

class SpeechCleanupTestAudioInputRegistrar final : public AudioInputRegistrar {
public:
	SpeechCleanupTestAudioInputRegistrar() : AudioInputRegistrar(QStringLiteral("SpeechCleanupTest"), -1000) {}

	AudioInput *create() override { return runtimeEnabled() ? new SpeechCleanupTestAudioInput() : nullptr; }
	const QVariant getDeviceChoice() override { return {}; }
	const QList< audioDevice > getDeviceChoices() override {
		return { audioDevice(QStringLiteral("Deterministic WAV input"), QVariant()) };
	}
	void setDeviceChoice(const QVariant &, Settings &) override {}
	bool canEcho(EchoCancelOptionID, const QString &) const override { return false; }
	bool isMicrophoneAccessDeniedByOS() override { return false; }
};

class SpeechCleanupTestAudioOutputRegistrar final : public AudioOutputRegistrar {
public:
	SpeechCleanupTestAudioOutputRegistrar() : AudioOutputRegistrar(QStringLiteral("SpeechCleanupTest"), -1000) {}

	AudioOutput *create() override { return runtimeEnabled() ? new SpeechCleanupTestAudioOutput() : nullptr; }
	const QVariant getDeviceChoice() override { return {}; }
	const QList< audioDevice > getDeviceChoices() override {
		return { audioDevice(QStringLiteral("Deterministic capture sink"), QVariant()) };
	}
	void setDeviceChoice(const QVariant &, Settings &) override {}
};

class SpeechCleanupTestAudioInit final : public DeferInit {
public:
	void initialize() override {
		if (!runtimeEnabled()) {
			return;
		}

		m_inputRegistrar  = std::make_unique< SpeechCleanupTestAudioInputRegistrar >();
		m_outputRegistrar = std::make_unique< SpeechCleanupTestAudioOutputRegistrar >();
		qInfo("SpeechCleanupTestAudio: deterministic E2E backend enabled for this process");
	}

	void destroy() override {
		m_outputRegistrar.reset();
		m_inputRegistrar.reset();
	}

private:
	std::unique_ptr< SpeechCleanupTestAudioInputRegistrar > m_inputRegistrar;
	std::unique_ptr< SpeechCleanupTestAudioOutputRegistrar > m_outputRegistrar;
};

SpeechCleanupTestAudioInit g_speechCleanupTestAudioInit;

} // namespace

SpeechCleanupTestAudioInput::SpeechCleanupTestAudioInput()
	: m_voiceContractEnabled(environmentValue(ENV_VOICE_CONTRACT) == QLatin1String("1")) {
	connect(this, &AudioInput::audioInputEncountered, this, &SpeechCleanupTestAudioInput::observePreOpusPcm,
			Qt::DirectConnection);
}

SpeechCleanupTestAudioInput::~SpeechCleanupTestAudioInput() {
	bRunning = false;
	wait();
}

void SpeechCleanupTestAudioInput::observeInputPcm(const float *samples, unsigned int sampleCount) {
	if (!m_voiceContractEnabled || !samples || sampleCount == 0) {
		return;
	}

	// The source backend feeds IEEE-754 floats into addMic(). Canonicalize their
	// exact bit patterns to little endian so the evidence is architecture-stable.
	std::array< quint32, TEST_FRAME_SIZE > canonical{};
	Q_ASSERT(sampleCount <= canonical.size());
	for (unsigned int index = 0; index < sampleCount; ++index) {
		quint32 bits = 0;
		static_assert(sizeof(bits) == sizeof(samples[index]));
		std::memcpy(&bits, &samples[index], sizeof(bits));
		canonical[index] = qToLittleEndian(bits);
	}
	m_inputPcmHash.addData(
		QByteArrayView(reinterpret_cast< const char * >(canonical.data()),
					   static_cast< qsizetype >(sampleCount) * static_cast< qsizetype >(sizeof(quint32))));
}

void SpeechCleanupTestAudioInput::submitInputFrame(float *samples, unsigned int sampleCount) {
	m_currentInputFrameIndex = m_nextInputFrameIndex;
	m_inputFrameArmed        = true;
	addMic(samples, sampleCount);
	m_inputFrameArmed = false;
	++m_nextInputFrameIndex;
}

void SpeechCleanupTestAudioInput::observePreOpusPcm(short *samples, unsigned int sampleCount,
												 unsigned int channelCount, unsigned int sampleRate, bool) {
	if (!m_preOpusCaptureValid) {
		return;
	}
	if (!samples || sampleCount != TEST_FRAME_SIZE || channelCount != 1 || sampleRate != TEST_SAMPLE_RATE) {
		m_preOpusCaptureValid = false;
		m_preOpusCaptureError = QStringLiteral("Pre-Opus observation had unexpected PCM geometry");
		return;
	}

	const std::uint64_t frameIndex = m_inputFrameArmed ? m_currentInputFrameIndex : m_nextInputFrameIndex++;
	const std::uint64_t preRollFrames = static_cast< std::uint64_t >(configuredPreRollFrames());
	if (frameIndex < preRollFrames) {
		return;
	}
	const std::uint64_t relativeFrame = frameIndex - preRollFrames;
	if (relativeFrame > (std::numeric_limits< std::size_t >::max() / TEST_FRAME_SIZE) - 1) {
		m_preOpusCaptureValid = false;
		m_preOpusCaptureError = QStringLiteral("Pre-Opus observation exceeded the addressable timeline");
		return;
	}
	const std::size_t offset = static_cast< std::size_t >(relativeFrame) * TEST_FRAME_SIZE;
	if (m_preOpusPcm.size() < offset + sampleCount) {
		m_preOpusPcm.resize(offset + sampleCount, 0);
	}
	std::copy_n(samples, sampleCount, m_preOpusPcm.begin() + static_cast< std::ptrdiff_t >(offset));
	++m_preOpusCallbacks;
}

void SpeechCleanupTestAudioInput::completeVadPreOpusSourceTimeline(std::uint64_t submittedSamples) {
	if (environmentValue(ENV_PRE_OPUS_WAV).isEmpty() || !m_preOpusCaptureValid
		|| Global::get().s.atTransmit != Settings::VAD) {
		return;
	}

	// audioInputEncountered is deliberately emitted only by the packet-producing
	// path. VAD therefore leaves no callback after it closes on trailing room
	// silence. The capture is nevertheless declared to use the source timeline,
	// so retain that intentionally non-transmitted interval as zero PCM. This is
	// bounded by the submitted source span: causal processor output beyond the
	// source must still be observed from real drain callbacks and can never be
	// manufactured by this completion step.
	const std::uint64_t preRollSamples =
		static_cast< std::uint64_t >(configuredPreRollFrames()) * TEST_FRAME_SIZE;
	if (submittedSamples < preRollSamples) {
		m_preOpusCaptureValid = false;
		m_preOpusCaptureError = QStringLiteral("Submitted source timeline is shorter than its pre-roll");
		return;
	}
	const std::uint64_t sourceTimelineSamples = submittedSamples - preRollSamples;
	if (sourceTimelineSamples > static_cast< std::uint64_t >(std::numeric_limits< std::size_t >::max())) {
		m_preOpusCaptureValid = false;
		m_preOpusCaptureError = QStringLiteral("Submitted source timeline exceeds the addressable capture");
		return;
	}
	if (m_preOpusPcm.size() < sourceTimelineSamples) {
		m_preOpusPcm.resize(static_cast< std::size_t >(sourceTimelineSamples), 0);
	}
}

bool SpeechCleanupTestAudioInput::writePreOpusCapture(QString *errorMessage) const {
	const QString path = environmentValue(ENV_PRE_OPUS_WAV);
	if (path.isEmpty()) {
		return true;
	}
	if (!m_preOpusCaptureValid || m_preOpusPcm.empty() || (m_preOpusPcm.size() % TEST_FRAME_SIZE) != 0) {
		if (errorMessage) {
			*errorMessage = !m_preOpusCaptureError.isEmpty()
							? m_preOpusCaptureError
							: QStringLiteral("Pre-Opus observation produced no complete callback frames");
		}
		return false;
	}
	const QFileInfo info(path);
	if (!QDir().mkpath(info.absolutePath())) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not create the pre-Opus capture directory: %1").arg(info.absolutePath());
		}
		return false;
	}
	SF_INFO fileInfo{};
	fileInfo.samplerate = static_cast< int >(TEST_SAMPLE_RATE);
	fileInfo.channels   = 1;
	fileInfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE *file       = openSoundFile(path, SFM_WRITE, &fileInfo);
	if (!file) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not open the pre-Opus capture WAV: %1")
							.arg(QString::fromLocal8Bit(sf_strerror(nullptr)));
		}
		return false;
	}
	const sf_count_t expected = static_cast< sf_count_t >(m_preOpusPcm.size());
	const sf_count_t written  = sf_writef_short(file, m_preOpusPcm.data(), expected);
	sf_write_sync(file);
	const int closeStatus = sf_close(file);
	if (written != expected || closeStatus != 0) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not finalize the complete pre-Opus capture WAV");
		}
		return false;
	}
	return true;
}

void SpeechCleanupTestAudioInput::beginVoiceContractObservation() {
	if (!m_voiceContractEnabled || m_voiceContractObservationStarted) {
		return;
	}
	m_inputPcmHash.reset();
	Mumble::SpeechCleanupE2E::beginOpusObservation();
	m_voiceContractObservationStarted = true;
}

void SpeechCleanupTestAudioInput::finishVoiceContractObservation() {
	if (!m_voiceContractObservationStarted || m_voiceContractObservationFinished) {
		return;
	}
	m_opusObservation                  = Mumble::SpeechCleanupE2E::finishOpusObservation();
	m_voiceContractObservationFinished = true;
}

void SpeechCleanupTestAudioInput::writeDone(bool ok, const QString &errorMessage, std::uint64_t sourceFrames,
											std::uint64_t submittedFrames) {
	QString finalError = errorMessage;
	QString preOpusError;
	completeVadPreOpusSourceTimeline(submittedFrames);
	if (!writePreOpusCapture(&preOpusError)) {
		ok = false;
		if (!finalError.isEmpty()) {
			finalError.append(QStringLiteral("; "));
		}
		finalError.append(preOpusError);
	}
	finishVoiceContractObservation();
	const Mumble::SpeechCleanup::Selection &selection = speechCleanupSelectionForDiagnostics();
	std::uint64_t deadlineMissCount                   = 0;
	std::uint64_t fallbackCount                       = speechCleanupUsedFallbackForDiagnostics() ? 1 : 0;
	QString activeModelSha256;
	QJsonObject inputEnhancementDiagnostics{ { QStringLiteral("available"), false } };
	if (const Mumble::InputEnhancement::Pipeline *pipeline = inputEnhancementPipelineForDiagnostics()) {
		const Mumble::InputEnhancement::Diagnostics diagnostics = pipeline->diagnostics();
		deadlineMissCount                                       = diagnostics.deadlineMisses();
		fallbackCount                                           = diagnostics.fallbackCount();
		activeModelSha256                                       = diagnostics.activeModelSha256();
		inputEnhancementDiagnostics                             = {
            { QStringLiteral("available"), true },
            { QStringLiteral("schema_version"),
										  static_cast< int >(Mumble::InputEnhancement::Diagnostics::schemaVersion) },
            { QStringLiteral("requested_recipe_id"), diagnostics.requestedRecipeId() },
            { QStringLiteral("recipe_revision"), static_cast< int >(diagnostics.recipeRevision()) },
            { QStringLiteral("requested_profile"), static_cast< int >(diagnostics.requestedProfile()) },
            { QStringLiteral("active_profile"), static_cast< int >(diagnostics.activeProfile()) },
            { QStringLiteral("active_engine"), static_cast< int >(diagnostics.activeEngine()) },
            { QStringLiteral("active_model_id"), diagnostics.activeModelId() },
            { QStringLiteral("active_model_sha256"), activeModelSha256 },
            { QStringLiteral("latency_budget_samples"), static_cast< int >(diagnostics.latencyBudgetSamples()) },
            { QStringLiteral("actual_latency_samples"), static_cast< int >(diagnostics.actualLatencySamples()) },
            { QStringLiteral("processed_frames"), QString::number(diagnostics.processedFrames()) },
            { QStringLiteral("neural_frames"), QString::number(diagnostics.neuralFrames()) },
            { QStringLiteral("deadline_misses"), QString::number(diagnostics.deadlineMisses()) },
            { QStringLiteral("fallback_count"), QString::number(diagnostics.fallbackCount()) },
            { QStringLiteral("fallback_active"), diagnostics.fallbackActive() },
            { QStringLiteral("fallback_reason"), inputEnhancementFallbackReasonName(diagnostics.fallbackReason()) },
            { QStringLiteral("total_processing_nanoseconds"),
										  QString::number(diagnostics.totalProcessingNanoseconds()) },
            { QStringLiteral("maximum_processing_nanoseconds"),
										  QString::number(diagnostics.maximumProcessingNanoseconds()) },
            { QStringLiteral("processing_p50_nanoseconds"), QString::number(diagnostics.processingP50Nanoseconds()) },
            { QStringLiteral("processing_p95_nanoseconds"), QString::number(diagnostics.processingP95Nanoseconds()) },
            { QStringLiteral("processing_p99_nanoseconds"), QString::number(diagnostics.processingP99Nanoseconds()) },
            { QStringLiteral("worker_processing_frames"), QString::number(diagnostics.workerProcessingFrames()) },
            { QStringLiteral("worker_total_processing_nanoseconds"),
										  QString::number(diagnostics.workerTotalProcessingNanoseconds()) },
            { QStringLiteral("worker_maximum_processing_nanoseconds"),
										  QString::number(diagnostics.workerMaximumProcessingNanoseconds()) },
            { QStringLiteral("worker_processing_p99_nanoseconds"),
										  QString::number(diagnostics.workerProcessingP99Nanoseconds()) },
            { QStringLiteral("worker_pending_frames"), static_cast< int >(pipeline->workerPendingFrames()) },
            { QStringLiteral("worker_scheduling_delay_frames"),
										  static_cast< int >(pipeline->workerSchedulingDelayFrames()) },
            { QStringLiteral("worker_scheduling_slack_frames"),
										  static_cast< int >(pipeline->workerSchedulingSlackFrames()) }
		};
	}
	if (Global::g_global_struct && Global::get().inputEnhancementPackageVerifier) {
		const auto *verifier = Global::get().inputEnhancementPackageVerifier;
		const Mumble::InputEnhancement::PackageVerificationReport report = verifier->report();
		inputEnhancementDiagnostics.insert(
			QStringLiteral("package_verification"),
			QJsonObject{ { QStringLiteral("ready"), report.ready },
						 { QStringLiteral("verified"), report.verified },
						 { QStringLiteral("unmanaged"), report.unmanaged },
						 { QStringLiteral("healthy"), verifier->verificationHealthy() },
						 { QStringLiteral("error"), static_cast< int >(report.error) },
						 { QStringLiteral("detail"), report.detail },
						 { QStringLiteral("catalog_revision"), verifier->catalogRevision() } });
	}
	if (Global::g_global_struct && Global::get().inputEnhancementPolicyController) {
		const auto *controller = Global::get().inputEnhancementPolicyController;
		const Mumble::InputEnhancement::EffectivePolicyState state = controller->effectiveState();
		inputEnhancementDiagnostics.insert(
			QStringLiteral("channel_policy"),
			QJsonObject{ { QStringLiteral("decision_ready"), controller->readyForHealthMarker() },
						 { QStringLiteral("decision_healthy"), controller->policyDecisionHealthy() },
						 { QStringLiteral("managed"), state.managedBySignedPolicy },
						 { QStringLiteral("verified"), state.hasVerifiedPolicy },
						 { QStringLiteral("available"), state.available },
						 { QStringLiteral("force_original"), state.forceOriginal },
						 { QStringLiteral("recommended_profile"),
						   static_cast< int >(state.recommendedProfile) },
						 { QStringLiteral("runtime_disabled"), Global::get().bDisableInputEnhancement },
						 { QStringLiteral("recovery_disabled"),
						   Global::get().bInputEnhancementRecoveryDisabled } });
	}
	if (Global::g_global_struct) {
		using namespace Mumble::InputEnhancement;
		const Profile requestedProfile =
			preferenceForDevice(Global::get().s.inputEnhancement, inputDeviceIdentity()).profile;
		const Profile effectiveProfile = inputEnhancementProfileForDiagnostics();
		QString effectiveReason = requestedProfile == effectiveProfile
			? QStringLiteral("requested_profile_active")
			: QStringLiteral("runtime_fallback");
		if (requestedProfile != Profile::Original) {
			if (const InputEnhancementPolicyController *controller =
					Global::get().inputEnhancementPolicyController) {
				switch (enhancedRuntimeBlockReason(controller->effectiveState(),
										  Global::get().bInputEnhancementRecoveryDisabled)) {
					case EnhancedRuntimeBlockReason::None:
						break;
					case EnhancedRuntimeBlockReason::ChannelUnavailable:
						effectiveReason = QStringLiteral("channel_policy_unavailable");
						break;
					case EnhancedRuntimeBlockReason::PolicyForcesOriginal:
						effectiveReason = QStringLiteral("channel_policy_force_original");
						break;
					case EnhancedRuntimeBlockReason::RecoveryDisabled:
						effectiveReason = QStringLiteral("local_recovery_switch");
						break;
				}
			} else if (Global::get().bDisableInputEnhancement) {
				effectiveReason = QStringLiteral("runtime_disabled");
			}
		}
		inputEnhancementDiagnostics.insert(QStringLiteral("configured_profile"),
									   static_cast< int >(requestedProfile));
		inputEnhancementDiagnostics.insert(QStringLiteral("effective_profile"),
									   static_cast< int >(effectiveProfile));
		inputEnhancementDiagnostics.insert(QStringLiteral("effective_reason"), effectiveReason);
	}
	// Keep model construction observable for every enhanced E2E run, including
	// preflight failures where no Pipeline object exists yet. Original and Light
	// must report zero; each fixed neural product profile must report exactly one.
	inputEnhancementDiagnostics.insert(
		QStringLiteral("model_initialization_attempts"),
		static_cast< int >(inputEnhancementModelInitializationAttemptsForDiagnostics()));
	inputEnhancementDiagnostics.insert(
		QStringLiteral("resolved_profile"), static_cast< int >(inputEnhancementProfileForDiagnostics()));
	QJsonObject result{
		{ QStringLiteral("ok"), ok },
		{ QStringLiteral("role"), QStringLiteral("input") },
		{ QStringLiteral("completed_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
		{ QStringLiteral("input_wav"), environmentValue(ENV_INPUT_WAV) },
		{ QStringLiteral("sample_rate"), static_cast< int >(TEST_SAMPLE_RATE) },
		{ QStringLiteral("source_frames"), QString::number(sourceFrames) },
		{ QStringLiteral("submitted_frames"), QString::number(submittedFrames) },
		{ QStringLiteral("tail_frames"), configuredTailFrames() },
		{ QStringLiteral("pre_roll_frames"), configuredPreRollFrames() },
		{ QStringLiteral("terminator_submitted"), m_terminatorSubmitted },
		{ QStringLiteral("drained_cleanup_samples"), static_cast< int >(m_drainedCleanupSamples) },
		{ QStringLiteral("pre_opus_capture"),
		  QJsonObject{ { QStringLiteral("enabled"), !environmentValue(ENV_PRE_OPUS_WAV).isEmpty() },
					   { QStringLiteral("path"), environmentValue(ENV_PRE_OPUS_WAV) },
					   { QStringLiteral("sample_frames"), QString::number(m_preOpusPcm.size()) },
					   { QStringLiteral("callbacks"), QString::number(m_preOpusCallbacks) },
					   { QStringLiteral("timeline_origin"), QStringLiteral("source-after-transmitted-preroll") },
					   { QStringLiteral("encoding"), QStringLiteral("signed-s16le") } } },
		{ QStringLiteral("effective_cleanup_mode"), noiseCancelModeName(noiseCancel) },
		{ QStringLiteral("requested_backend"), Mumble::SpeechCleanup::backendDisplayName(selection.backend) },
		{ QStringLiteral("requested_model_id"), selection.modelId },
		{ QStringLiteral("processor_ready"), speechCleanupReadyForDiagnostics() },
		{ QStringLiteral("active_model_id"), speechCleanupActiveModelIdForDiagnostics() },
		{ QStringLiteral("active_model_path"), speechCleanupActiveModelPathForDiagnostics() },
		{ QStringLiteral("used_fallback"), speechCleanupUsedFallbackForDiagnostics() },
		{ QStringLiteral("reported_latency_samples"), static_cast< int >(speechCleanupLatencyForDiagnostics()) },
		{ QStringLiteral("input_enhancement"), inputEnhancementDiagnostics },
		{ QStringLiteral("error"), finalError }
	};
	if (m_voiceContractEnabled) {
		const bool usesLegacy                                 = usesLegacyInputEnhancementForDiagnostics();
		const Mumble::InputEnhancement::Profile activeProfile = inputEnhancementProfileForDiagnostics();
		const QString enhancementProfile =
			usesLegacy ? QStringLiteral("Legacy") : inputEnhancementProfileName(activeProfile);
		const QString implementation =
			usesLegacy ? QStringLiteral("legacy")
					   : (activeProfile == Mumble::InputEnhancement::Profile::Original ? QStringLiteral("original")
																					   : QStringLiteral("product"));
		result.insert(
			QStringLiteral("voice_contract"),
			QJsonObject{ { QStringLiteral("schema_version"), 1 },
						 { QStringLiteral("implementation"), implementation },
						 { QStringLiteral("enhancement_profile"), enhancementProfile },
						 { QStringLiteral("bitrate_bps"), iAudioQuality },
						 { QStringLiteral("frames_per_packet"), iAudioFrames },
						 { QStringLiteral("transmit_mode"), transmitModeName(Global::get().s.atTransmit) },
						 { QStringLiteral("input_pcm_sha256"), QString::fromLatin1(m_inputPcmHash.result().toHex()) },
						 { QStringLiteral("pre_opus_pcm_sha256"),
						   QString::fromLatin1(m_opusObservation.preOpusPcmSha256.toHex()) },
						 { QStringLiteral("opus_packets_sha256"),
						   QString::fromLatin1(m_opusObservation.opusPacketsSha256.toHex()) },
						 { QStringLiteral("packet_count"), static_cast< int >(m_opusObservation.packetCount) },
						 { QStringLiteral("terminator_count"), m_terminatorSubmitted ? 1 : 0 },
						 { QStringLiteral("model_initialization_attempts"),
						   static_cast< int >(inputEnhancementModelInitializationAttemptsForDiagnostics()) },
						 { QStringLiteral("algorithmic_latency_samples"),
						   static_cast< int >(speechCleanupLatencyForDiagnostics()) },
						 { QStringLiteral("active_model_sha256"), activeModelSha256 },
						 { QStringLiteral("fallback_count"), static_cast< int >(fallbackCount) },
						 { QStringLiteral("deadline_miss_count"), static_cast< int >(deadlineMissCount) },
						 { QStringLiteral("ptt_hold_activated"), m_pttHoldActivated },
						 { QStringLiteral("input_pcm_encoding"), QStringLiteral("ieee754-f32le") },
						 { QStringLiteral("pre_opus_pcm_encoding"), QStringLiteral("signed-s16le") },
						 { QStringLiteral("opus_packet_hash_framing"), QStringLiteral("u32le-length+payload") } });
	}

	QString writeError;
	if (!writeJsonAtomically(environmentValue(ENV_INPUT_DONE), result, &writeError)) {
		qWarning("SpeechCleanupTestAudioInput: could not write completion artifact: %s", qUtf8Printable(writeError));
	}
}

void SpeechCleanupTestAudioInput::run() {
	iMicChannels  = 1;
	iMicFreq      = TEST_SAMPLE_RATE;
	eMicFormat    = SampleFloat;
	iEchoChannels = 0;
	iEchoFreq     = TEST_SAMPLE_RATE;
	eEchoFormat   = SampleFloat;
	initializeMixer();

	const QString inputPath = environmentValue(ENV_INPUT_WAV);
	if (inputPath.isEmpty()) {
		// Receiver-only processes still need a live, deterministic input backend.
		while (bRunning) {
			QThread::msleep(20);
		}
		return;
	}

	const QString startGatePath = environmentValue(ENV_START_GATE);
	const QString inputDonePath = environmentValue(ENV_INPUT_DONE);
	if (startGatePath.isEmpty() || inputDonePath.isEmpty()) {
		const QString error = QStringLiteral("Sender requires start-gate and input-done paths");
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning)
			QThread::msleep(20);
		return;
	}

	while (bRunning) {
		const ServerHandlerPtr server = Global::get().sh;
		if (server && server->hasSynchronized() && gateMatchesRunToken(startGatePath)) {
			break;
		}
		QThread::msleep(20);
	}
	if (!bRunning) {
		return;
	}

	QFileInfo inputInfo(inputPath);
	if (!inputInfo.isFile() || !inputInfo.isReadable()) {
		const QString error = QStringLiteral("Input WAV is not a readable regular file: %1").arg(inputPath);
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning)
			QThread::msleep(20);
		return;
	}

	SF_INFO info{};
	SNDFILE *inputFile = openSoundFile(inputPath, SFM_READ, &info);
	if (!inputFile) {
		const QString error =
			QStringLiteral("Could not open input WAV: %1").arg(QString::fromLocal8Bit(sf_strerror(nullptr)));
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning)
			QThread::msleep(20);
		return;
	}

	if (info.samplerate != static_cast< int >(TEST_SAMPLE_RATE) || info.channels != 1) {
		const QString error = QStringLiteral("Input WAV must be mono 48000 Hz (got %1 channels at %2 Hz)")
								  .arg(info.channels)
								  .arg(info.samplerate);
		sf_close(inputFile);
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning)
			QThread::msleep(20);
		return;
	}
	const bool pttHoldRequested = environmentValue(ENV_HOLD_PTT) == QLatin1String("1");
	if (pttHoldRequested && Global::get().s.atTransmit != Settings::PushToTalk) {
		const QString error = QStringLiteral("E2E PTT hold was requested while transmit mode was not PushToTalk");
		sf_close(inputFile);
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning)
			QThread::msleep(20);
		return;
	}
	ScopedE2EPushToTalk pttHold(pttHoldRequested);
	m_pttHoldActivated = pttHold.active();
	beginVoiceContractObservation();

	qInfo("SpeechCleanupTestAudioInput: streaming %s", qUtf8Printable(inputPath));
	std::array< float, TEST_FRAME_SIZE > frame{};
	std::uint64_t sourceFrames    = 0;
	std::uint64_t submittedFrames = 0;
	bool enteredTransmitPath      = isTransmitting();
	auto nextDeadline             = std::chrono::steady_clock::now();

	// The tuning loop may request a transmitted silent pre-roll so the generic
	// Opus/jitter startup transient is kept separate from cleanup quality. The
	// receiver discards exactly these frames from the capture artifact; this is
	// independent of the zero-tail setting used to prove causal drain behavior.
	frame.fill(0.0f);
	for (int index = 0; bRunning && index < configuredPreRollFrames(); ++index) {
		observeInputPcm(frame.data(), static_cast< unsigned int >(frame.size()));
		submitInputFrame(frame.data(), static_cast< unsigned int >(frame.size()));
		enteredTransmitPath = enteredTransmitPath || isTransmitting();
		submittedFrames += frame.size();
		paceRealtime(nextDeadline);
	}

	while (bRunning) {
		frame.fill(0.0f);
		const sf_count_t readFrames = sf_readf_float(inputFile, frame.data(), static_cast< sf_count_t >(frame.size()));
		if (readFrames < 0) {
			const QString error = QStringLiteral("Failed while reading input WAV: %1")
									  .arg(QString::fromLocal8Bit(sf_strerror(inputFile)));
			sf_close(inputFile);
			pttHold.release();
			writeDone(false, error, sourceFrames, submittedFrames);
			while (bRunning)
				QThread::msleep(20);
			return;
		}
		if (readFrames == 0) {
			break;
		}

		sourceFrames += static_cast< std::uint64_t >(readFrames);
		observeInputPcm(frame.data(), static_cast< unsigned int >(frame.size()));
		submitInputFrame(frame.data(), static_cast< unsigned int >(frame.size()));
		enteredTransmitPath = enteredTransmitPath || isTransmitting();
		submittedFrames += frame.size();
		paceRealtime(nextDeadline);
	}

	sf_close(inputFile);

	frame.fill(0.0f);
	for (int index = 0; bRunning && index < configuredTailFrames(); ++index) {
		observeInputPcm(frame.data(), static_cast< unsigned int >(frame.size()));
		submitInputFrame(frame.data(), static_cast< unsigned int >(frame.size()));
		enteredTransmitPath = enteredTransmitPath || isTransmitting();
		submittedFrames += frame.size();
		paceRealtime(nextDeadline);
	}
	if (bRunning && !enteredTransmitPath) {
		pttHold.release();
		const QString error = QStringLiteral("Sender never entered the selected transmit-mode path");
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, sourceFrames, submittedFrames);
		while (bRunning)
			QThread::msleep(20);
		return;
	}
	if (bRunning) {
		pttHold.release();
		// End the selected transmit path explicitly so the receiver observes a
		// real Opus terminator and exercises causal cleanup-tail draining instead
		// of aging out through jitter timeout.
		m_drainedCleanupSamples = finishSpeechCleanupE2ETransmission();
		m_terminatorSubmitted   = true;
	}

	if (bRunning) {
		qInfo("SpeechCleanupTestAudioInput: completed %llu source frames and %llu submitted frames",
			  static_cast< unsigned long long >(sourceFrames), static_cast< unsigned long long >(submittedFrames));
		writeDone(true, {}, sourceFrames, submittedFrames);
	}

	while (bRunning) {
		QThread::msleep(20);
	}
}

SpeechCleanupTestAudioOutput::SpeechCleanupTestAudioOutput()
	: m_capturePath(environmentValue(ENV_CAPTURE_WAV)), m_captureSender(environmentValue(ENV_CAPTURE_SENDER)),
	  m_captureFramesToSkip(static_cast< std::uint64_t >(configuredPreRollFrames()) * TEST_FRAME_SIZE) {
	connect(this, &AudioOutput::audioSourceFetched, this, &SpeechCleanupTestAudioOutput::captureSource,
			Qt::DirectConnection);
}

SpeechCleanupTestAudioOutput::~SpeechCleanupTestAudioOutput() {
	bRunning = false;
	wait();
}

bool SpeechCleanupTestAudioOutput::openCapture(QString *errorMessage) {
	if (m_capturePath.isEmpty()) {
		return true;
	}
	if (m_captureSender.isEmpty()) {
		if (errorMessage)
			*errorMessage = QStringLiteral("Capture sender name is required");
		return false;
	}
	if (Global::get().s.remoteSpeechCleanupEnabled) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Receiver cleanup must be disabled for input-enhancement E2E qualification");
		}
		return false;
	}
	if (environmentValue(ENV_STOP_GATE).isEmpty() || environmentValue(ENV_CAPTURE_DONE).isEmpty()) {
		if (errorMessage)
			*errorMessage = QStringLiteral("Capture requires stop-gate and completion paths");
		return false;
	}

	const QFileInfo captureInfo(m_capturePath);
	if (!QDir().mkpath(captureInfo.absolutePath())) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not create capture directory '%1'").arg(captureInfo.absolutePath());
		}
		return false;
	}

	SF_INFO info{};
	info.samplerate = static_cast< int >(TEST_SAMPLE_RATE);
	info.channels   = 1;
	info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	m_captureFile   = openSoundFile(m_capturePath, SFM_WRITE, &info);
	if (!m_captureFile) {
		if (errorMessage) {
			*errorMessage =
				QStringLiteral("Could not open capture WAV: %1").arg(QString::fromLocal8Bit(sf_strerror(nullptr)));
		}
		return false;
	}

	m_monoBuffer.resize(TEST_FRAME_SIZE);
	qInfo("SpeechCleanupTestAudioOutput: capturing sender '%s' to %s", qUtf8Printable(m_captureSender),
		  qUtf8Printable(m_capturePath));
	return true;
}

void SpeechCleanupTestAudioOutput::closeCapture() {
	if (m_captureFile) {
		sf_write_sync(m_captureFile);
		sf_close(m_captureFile);
		m_captureFile = nullptr;
	}
}

void SpeechCleanupTestAudioOutput::captureSource(float *outputPCM, unsigned int sampleCount, unsigned int channelCount,
												 unsigned int sampleRate, bool isSpeech, const ClientUser *user) {
	if (!m_captureFile || !outputPCM || !isSpeech || !user || sampleCount == 0 || channelCount == 0
		|| sampleRate != TEST_SAMPLE_RATE || user->qsName != m_captureSender) {
		return;
	}

	const unsigned int skippedFrames =
		static_cast< unsigned int >(std::min< std::uint64_t >(m_captureFramesToSkip, sampleCount));
	m_captureFramesToSkip -= skippedFrames;
	m_discardedPreRollFrames += skippedFrames;
	if (skippedFrames == sampleCount) {
		return;
	}

	const unsigned int capturedFrames = sampleCount - skippedFrames;
	if (m_monoBuffer.size() < capturedFrames) {
		m_monoBuffer.resize(capturedFrames);
	}

	for (unsigned int frameIndex = 0; frameIndex < capturedFrames; ++frameIndex) {
		float mixed = 0.0f;
		for (unsigned int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
			mixed += outputPCM[(frameIndex + skippedFrames) * channelCount + channelIndex];
		}
		m_monoBuffer[frameIndex] = mixed / static_cast< float >(channelCount);
	}

	const sf_count_t written =
		sf_writef_float(m_captureFile, m_monoBuffer.data(), static_cast< sf_count_t >(capturedFrames));
	if (written != static_cast< sf_count_t >(capturedFrames)) {
		qWarning("SpeechCleanupTestAudioOutput: short capture write (%lld of %u frames)",
				 static_cast< long long >(written), capturedFrames);
		return;
	}

	m_capturedFrames += capturedFrames;
	++m_captureCallbacks;
}

void SpeechCleanupTestAudioOutput::writeDone(bool ok, const QString &errorMessage, bool stopGateObserved) const {
	if (m_capturePath.isEmpty()) {
		return;
	}

	const QJsonObject remoteCleanup{
		{ QStringLiteral("diagnostics_captured"), true },
		{ QStringLiteral("requested_enabled"), false },
		{ QStringLiteral("processor_ready"), false },
		{ QStringLiteral("used_fallback"), false },
		{ QStringLiteral("reported_latency_samples"), 0 },
		{ QStringLiteral("reported_latency_ms"), 0.0 },
		{ QStringLiteral("active"), false },
		{ QStringLiteral("was_applied"), false },
		{ QStringLiteral("drained_samples"), 0 },
		{ QStringLiteral("drain_completed"), true },
		{ QStringLiteral("forced_off"), true },
	};

	QJsonObject result{ { QStringLiteral("ok"), ok },
						{ QStringLiteral("role"), QStringLiteral("capture") },
						{ QStringLiteral("completed_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
						{ QStringLiteral("capture_wav"), m_capturePath },
						{ QStringLiteral("capture_sender"), m_captureSender },
						{ QStringLiteral("sample_rate"), static_cast< int >(TEST_SAMPLE_RATE) },
						{ QStringLiteral("channels"), 1 },
						{ QStringLiteral("captured_frames"), QString::number(m_capturedFrames) },
						{ QStringLiteral("capture_callbacks"), QString::number(m_captureCallbacks) },
						{ QStringLiteral("discarded_pre_roll_frames"), QString::number(m_discardedPreRollFrames) },
						{ QStringLiteral("stop_gate_observed"), stopGateObserved },
						{ QStringLiteral("remote_cleanup"), remoteCleanup },
						{ QStringLiteral("error"), errorMessage } };

	QString writeError;
	if (!writeJsonAtomically(environmentValue(ENV_CAPTURE_DONE), result, &writeError)) {
		qWarning("SpeechCleanupTestAudioOutput: could not write completion artifact: %s", qUtf8Printable(writeError));
	}
}

void SpeechCleanupTestAudioOutput::run() {
	eSampleFormat = SampleFloat;
	iMixerFreq    = TEST_SAMPLE_RATE;
	iChannels     = 2;
	setBufferSize(TEST_FRAME_SIZE);
	const unsigned int channelMasks[] = { SPEAKER_FRONT_LEFT, SPEAKER_FRONT_RIGHT };
	initializeMixer(channelMasks);

	QString captureError;
	if (!openCapture(&captureError)) {
		qWarning("SpeechCleanupTestAudioOutput: %s", qUtf8Printable(captureError));
		writeDone(false, captureError, false);
		while (bRunning)
			QThread::msleep(20);
		return;
	}

	std::array< float, TEST_FRAME_SIZE * 2 > output{};
	auto nextDeadline     = std::chrono::steady_clock::now();
	bool stopGateObserved = false;

	while (bRunning) {
		output.fill(0.0f);
		mix(output.data(), TEST_FRAME_SIZE);

		if (!m_capturePath.isEmpty() && gateMatchesRunToken(environmentValue(ENV_STOP_GATE))) {
			stopGateObserved = true;
			break;
		}
		paceRealtime(nextDeadline);
	}

	closeCapture();
	if (!m_capturePath.isEmpty()) {
		const bool ok = stopGateObserved && m_capturedFrames > 0;
		QString error;
		if (!stopGateObserved) {
			error = QStringLiteral("Capture stopped before the stop gate was observed");
		} else if (m_capturedFrames == 0) {
			error = QStringLiteral("No matching speech frames were captured");
		}
		writeDone(ok, error, stopGateObserved);
		qInfo("SpeechCleanupTestAudioOutput: finalized %llu frames from %llu callbacks",
			  static_cast< unsigned long long >(m_capturedFrames),
			  static_cast< unsigned long long >(m_captureCallbacks));
	}

	while (bRunning) {
		QThread::msleep(20);
	}
}
