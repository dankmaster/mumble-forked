// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioInput.h"

#include "API.h"
#include "Audio.h"
#include "AudioOutput.h"
#include "InputEnhancementAutoV2.h"
#include "InputEnhancementPackageVerifier.h"
#include "InputEnhancementPolicyController.h"
#include "MainWindow.h"
#include "MumbleProtocol.h"
#include "NetworkConfig.h"
#include "PacketDataStream.h"
#include "PluginManager.h"
#include "ServerHandler.h"
#include "SpeechCleanup.h"
#include "SpeechCleanupProcessor.h"
#include "User.h"
#include "Utils.h"
#include "VoiceActivationDebugCapture.h"
#include "VoiceRecorder.h"
#include "WebRTCAudioEchoCanceller.h"
#include "Global.h"

#include <QtCore/QDateTime>

#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
// Preserve the production encoder source verbatim while allowing the explicit
// E2E build to attest the exact PCM and payload bytes at the libopus boundary.
#	define opus_encode mumble_speech_cleanup_e2e_opus_encode
#endif
#include <opus.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <limits>
#include <span>
#include <thread>

/// Clip the given float value to a range that can be safely converted into a short (without causing integer overflow)
static short clampFloatSample(float v) {
	return static_cast< short >(std::clamp(v, static_cast< float >(std::numeric_limits< short >::min()),
										   static_cast< float >(std::numeric_limits< short >::max())));
}

static Mumble::InputEnhancement::CpuClass currentInputEnhancementCpuClass(
	const Mumble::InputEnhancement::Profile profile = Mumble::InputEnhancement::Profile::Original) {
	using namespace Mumble::InputEnhancement;
	const InputEnhancementPackageVerifier *verifier =
		Global::g_global_struct ? Global::get().inputEnhancementPackageVerifier : nullptr;
	if (!verifier) {
		return CpuClass::Low;
	}
	CpuClass measured = CpuClass::Low;
	if (profile == Profile::Auto) {
		const AutoV2::CapabilityProbeKey key =
			AutoV2::currentCapabilityProbeKey(verifier->runtimePayloadFingerprint());
		const AutoV2::CapabilityProbeResult result = AutoV2::cachedCapabilityProbe(key);
		measured = result.valid ? result.cpuTier : CpuClass::Low;
	} else if (profile == Profile::Quality || profile == Profile::VoiceFocus) {
		measured = verifier->manualProfileCpuClass();
	} else if (profile == Profile::Balanced && BackendAvailability::compiled().rnnoise) {
		measured = CpuClass::Standard;
	}
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
	measured = cpuClassWithAuthenticatedE2EOverride(
		measured, qEnvironmentVariable("MUMBLE_SPEECH_CLEANUP_E2E_ENABLE") == QLatin1String("1"),
		!qEnvironmentVariable("MUMBLE_SPEECH_CLEANUP_E2E_TOKEN").trimmed().isEmpty(),
		qEnvironmentVariable("MUMBLE_INPUT_ENHANCEMENT_E2E_CPU_CLASS"));
#endif
	return measured;
}

class CalibrationPacketPathLease final {
public:
	explicit CalibrationPacketPathLease(Mumble::InputEnhancement::CalibrationTransmissionBlock &block) noexcept
		: m_block(block), m_acquired(block.tryEnterPacketPath()) {}
	~CalibrationPacketPathLease() {
		if (m_acquired) {
			m_block.leavePacketPath();
		}
	}

	CalibrationPacketPathLease(const CalibrationPacketPathLease &)            = delete;
	CalibrationPacketPathLease &operator=(const CalibrationPacketPathLease &) = delete;
	explicit operator bool() const noexcept { return m_acquired; }

private:
	Mumble::InputEnhancement::CalibrationTransmissionBlock &m_block;
	bool m_acquired;
};

struct InputGateParameters {
	float openAmplitude      = 0.0f;
	float closeAmplitude     = 0.0f;
	float openSpeechProb     = 0.0f;
	float closeSpeechProb    = 0.0f;
	int requiredAttackFrames = 1;
	int releaseHoldFrames    = 0;
};

static InputGateParameters inputGateParametersFor(Settings::InputGateMode mode) {
	switch (mode) {
		case Settings::InputGateBalanced:
			return { 0.18f, 0.10f, 0.35f, 0.20f, 1, 12 };
		case Settings::InputGateStrict:
			return { 0.28f, 0.16f, 0.55f, 0.35f, 2, 8 };
		case Settings::InputGateOff:
			break;
	}

	return {};
}

float AudioInput::amplitudeVoiceActivityLevel() const {
	return std::clamp(1.0f + dPeakCleanMic / 96.0f, 0.0f, 1.0f);
}

float AudioInput::VoiceActivitySnapshot::amplitudeLevel() const {
	return std::clamp(1.0f + peakCleanMicDb / 96.0f, 0.0f, 1.0f);
}

bool AudioInput::VoiceActivitySnapshot::hasProcessedInput() const {
	return peakCleanMicDb < 0.0f || peakSignalDb < 0.0f || speechProbability > 0.0f || transmitting;
}

float AudioInput::voiceActivityLevel() const {
	return voiceActivityLevelFor(Global::get().s.vsVAD, amplitudeVoiceActivityLevel(), fSpeechProb);
}

float AudioInput::voiceActivityLevelFor(Settings::VADSource source, float amplitudeLevel, float speechProbability) {
	amplitudeLevel    = std::clamp(amplitudeLevel, 0.0f, 1.0f);
	speechProbability = std::clamp(speechProbability, 0.0f, 1.0f);

	switch (source) {
		case Settings::SignalToNoise:
			return speechProbability;
		case Settings::Hybrid:
			return std::min(amplitudeLevel, speechProbability);
		case Settings::Amplitude:
			return amplitudeLevel;
	}

	return amplitudeLevel;
}

bool AudioInput::voiceActivityTriggers(float level, float silenceThreshold, float speechThreshold,
									   bool wasTransmitting) {
	level            = std::clamp(level, 0.0f, 1.0f);
	silenceThreshold = std::clamp(silenceThreshold, 0.0f, 1.0f);
	speechThreshold  = std::clamp(speechThreshold, 0.0f, 1.0f);
	if (speechThreshold < silenceThreshold) {
		std::swap(speechThreshold, silenceThreshold);
	}

	return level > speechThreshold || (level > silenceThreshold && wasTransmitting);
}

bool AudioInput::inputGateAllowsSpeechFor(Settings::InputGateMode mode, Settings::VADSource source,
										  bool candidateSpeech, float amplitudeLevel, float speechProbability,
										  bool &gateOpen, int &attackFrames, int &releaseFrames) {
	if (mode == Settings::InputGateOff) {
		gateOpen      = false;
		attackFrames  = 0;
		releaseFrames = 0;
		return candidateSpeech;
	}

	const InputGateParameters parameters = inputGateParametersFor(mode);
	amplitudeLevel                       = std::clamp(amplitudeLevel, 0.0f, 1.0f);
	speechProbability                    = std::clamp(speechProbability, 0.0f, 1.0f);
	const auto sourceClearsGate = [source, amplitudeLevel, speechProbability](float amplitudeFloor,
																			 float speechProbabilityFloor) {
		switch (source) {
			case Settings::Amplitude:
				return amplitudeLevel >= amplitudeFloor;
			case Settings::SignalToNoise:
				return speechProbability >= speechProbabilityFloor;
			case Settings::Hybrid:
				return amplitudeLevel >= amplitudeFloor && speechProbability >= speechProbabilityFloor;
		}

		return false;
	};
	const bool openCandidate =
		candidateSpeech && sourceClearsGate(parameters.openAmplitude, parameters.openSpeechProb);
	const bool keepCandidate =
		candidateSpeech && sourceClearsGate(parameters.closeAmplitude, parameters.closeSpeechProb);

	if (gateOpen) {
		if (keepCandidate) {
			attackFrames  = parameters.requiredAttackFrames;
			releaseFrames = 0;
			return true;
		}

		++releaseFrames;
		if (releaseFrames <= parameters.releaseHoldFrames) {
			return true;
		}

		gateOpen      = false;
		attackFrames  = 0;
		releaseFrames = 0;
		return false;
	}

	if (openCandidate) {
		attackFrames  = std::min(attackFrames + 1, parameters.requiredAttackFrames);
		releaseFrames = 0;
		if (attackFrames >= parameters.requiredAttackFrames) {
			gateOpen = true;
			return true;
		}
	} else {
		attackFrames  = 0;
		releaseFrames = 0;
	}

	return false;
}

bool AudioInput::inputGateAllowsSpeech(bool candidateSpeech, float amplitudeLevel, float speechProbability) {
	return inputGateAllowsSpeechFor(Global::get().s.inputGateMode, Global::get().s.vsVAD, candidateSpeech,
									amplitudeLevel, speechProbability, m_inputGateOpen, m_inputGateAttackFrames,
									m_inputGateReleaseFrames);
}

void Resynchronizer::addMic(short *mic) {
	bool drop = false;
	{
		std::unique_lock< std::mutex > l(m);
		micQueue.push_back(mic);
		switch (state) {
			case S0:
				state = S1a;
				break;
			case S1a:
				state = S2;
				break;
			case S1b:
				state = S2;
				break;
			case S2:
				state = S3;
				break;
			case S3:
				state = S4a;
				break;
			case S4a:
				state = S5;
				break;
			case S4b:
				drop = true;
				break;
			case S5:
				drop = true;
				break;
		}
		if (drop) {
			delete[] micQueue.front();
			micQueue.pop_front();
		}
	}
	if (bDebugPrintQueue) {
		if (drop)
			qWarning("Resynchronizer::addMic(): dropped microphone chunk due to overflow");
		printQueue('+');
	}
}

AudioChunk Resynchronizer::addSpeaker(short *speaker) {
	AudioChunk result;
	bool drop = false;
	{
		std::unique_lock< std::mutex > l(m);
		switch (state) {
			case S0:
				drop = true;
				break;
			case S1a:
				drop = true;
				break;
			case S1b:
				state = S0;
				break;
			case S2:
				state = S1b;
				break;
			case S3:
				state = S2;
				break;
			case S4a:
				state = S3;
				break;
			case S4b:
				state = S3;
				break;
			case S5:
				state = S4b;
				break;
		}
		if (drop == false) {
			result = AudioChunk(micQueue.front(), speaker);
			micQueue.pop_front();
		}
	}
	if (drop)
		delete[] speaker;
	if (bDebugPrintQueue) {
		if (drop)
			qWarning("Resynchronizer::addSpeaker(): dropped speaker chunk due to underflow");
		printQueue('-');
	}
	return result;
}

void Resynchronizer::reset() {
	if (bDebugPrintQueue)
		qWarning("Resetting echo queue");
	std::unique_lock< std::mutex > l(m);
	state = S0;
	while (!micQueue.empty()) {
		delete[] micQueue.front();
		micQueue.pop_front();
	}
}

Resynchronizer::~Resynchronizer() {
	reset();
}

void Resynchronizer::printQueue(char who) {
	unsigned int mic;
	{
		std::unique_lock< std::mutex > l(m);
		mic = static_cast< unsigned int >(micQueue.size());
	}
	std::string line;
	line.reserve(32);
	line += who;
	line += " Echo queue [";
	for (unsigned int i = 0; i < 5; i++)
		line += i < mic ? '#' : ' ';
	line += "]\r";
	// This relies on \r to retrace always on the same line, can't use qWarining
	printf("%s", line.c_str());
	fflush(stdout);
}

// Remember that we cannot use static member classes that are not pointers, as the constructor
// for AudioInputRegistrar() might be called before they are initialized, as the constructor
// is called from global initialization.
// Hence, we allocate upon first call.

QMap< QString, AudioInputRegistrar * > *AudioInputRegistrar::qmNew;
QString AudioInputRegistrar::current = QString();

AudioInputRegistrar::AudioInputRegistrar(const QString &n, int p) : name(n), priority(p), echoOptions() {
	if (!qmNew)
		qmNew = new QMap< QString, AudioInputRegistrar * >();
	qmNew->insert(name, this);
}

AudioInputRegistrar::~AudioInputRegistrar() {
	qmNew->remove(name);
}

Mumble::InputEnhancement::DeviceIdentity AudioInputRegistrar::resolveDeviceIdentity() {
	return resolveDeviceIdentity(Global::get().s);
}

Mumble::InputEnhancement::DeviceIdentity AudioInputRegistrar::resolveDeviceIdentity(const Settings &settings) {
	Mumble::InputEnhancement::DeviceIdentity identity;
	identity.backendId = name;
	QVariant configuredChoice;
	if (name == QLatin1String("WASAPI")) {
		configuredChoice = settings.qsWASAPIInput;
	} else if (name == QLatin1String("ALSA")) {
		configuredChoice = settings.qsALSAInput;
	} else if (name == QLatin1String("PulseAudio")) {
		configuredChoice = settings.qsPulseAudioInput;
	} else if (name == QLatin1String("PipeWire")) {
		configuredChoice = settings.pipeWireInput;
	} else if (name == QLatin1String("PortAudio")) {
		configuredChoice = settings.iPortAudioInput;
	} else if (name == QLatin1String("CoreAudio")) {
		configuredChoice = settings.qsCoreAudioInput;
	} else if (name == QLatin1String("OSS")) {
		configuredChoice = settings.qsOSSInput;
	} else {
		configuredChoice = getDeviceChoice();
	}
	identity.physicalId           = configuredChoice.toString();
	identity.followsSystemDefault = identity.physicalId.isEmpty()
									|| identity.physicalId.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0;

	for (const audioDevice &choice : getDeviceChoices()) {
		if (choice.second == configuredChoice || choice.second.toString() == configuredChoice.toString()) {
			identity.displayName = choice.first;
			break;
		}
	}

	const bool backendExposesStableExplicitId = name == QLatin1String("WASAPI") || name == QLatin1String("CoreAudio")
												|| name == QLatin1String("PulseAudio") || name == QLatin1String("ALSA");
	identity.stable =
		backendExposesStableExplicitId && !identity.followsSystemDefault && !identity.physicalId.isEmpty();
	return identity;
}

Mumble::InputEnhancement::DeviceIdentity AudioInputRegistrar::resolveDeviceIdentity(const QString &backend,
																					const Settings &settings) {
	if (qmNew && qmNew->contains(backend)) {
		return qmNew->value(backend)->resolveDeviceIdentity(settings);
	}
	Mumble::InputEnhancement::DeviceIdentity identity;
	identity.backendId = backend;
	identity.stable    = false;
	return identity;
}

Mumble::InputEnhancement::DeviceIdentity AudioInputRegistrar::resolveCurrentDeviceIdentity() {
	const QString backend = current.isEmpty() ? Global::get().s.qsAudioInput : current;
	return resolveDeviceIdentity(backend, Global::get().s);
}

AudioInputPtr AudioInputRegistrar::newFromChoice(QString choice) {
	if (!qmNew)
		return AudioInputPtr();

	if (!choice.isEmpty() && qmNew->contains(choice)) {
		Global::get().s.qsAudioInput = choice;
		current                      = choice;
		return AudioInputPtr(qmNew->value(current)->create());
	}
	choice = Global::get().s.qsAudioInput;
	if (qmNew->contains(choice)) {
		current = choice;
		return AudioInputPtr(qmNew->value(choice)->create());
	}

	AudioInputRegistrar *r = nullptr;
	for (AudioInputRegistrar *air : *qmNew)
		if (!r || (air->priority > r->priority))
			r = air;
	if (r) {
		current = r->name;
		return AudioInputPtr(r->create());
	}
	return AudioInputPtr();
}

bool AudioInputRegistrar::canExclusive() const {
	return false;
}

bool AudioInputRegistrar::isMicrophoneAccessDeniedByOS() {
	return false;
}

AudioInput::AudioInput()
	: m_inputDeviceIdentity(AudioInputRegistrar::resolveCurrentDeviceIdentity()),
	  opusBuffer(
		  static_cast< std::size_t >(clampFramesPerPacket(Global::get().s.iFramesPerPacket) * (SAMPLE_RATE / 100))) {
	bDebugDumpInput         = Global::get().bDebugDumpInput;
	resync.bDebugPrintQueue = Global::get().bDebugPrintQueue;
	if (bDebugDumpInput) {
		outMic.open("raw_microphone_dump", std::ios::binary);
		outSpeaker.open("speaker_dump", std::ios::binary);
		outProcessed.open("processed_microphone_dump", std::ios::binary);
	}

	adjustBandwidth(Global::get().iMaxBandwidth, iAudioQuality, iAudioFrames, bAllowLowDelay);

	Global::get().iAudioBandwidth = getNetworkBandwidth(iAudioQuality, iAudioFrames);

	m_codec = Mumble::Protocol::AudioCodec::Opus;

	activityState = ActivityStateActive;
	opusState     = nullptr;

	if (bAllowLowDelay && iAudioQuality >= 64000) { // > 64 kbit/s bitrate and low delay allowed
		opusState = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_RESTRICTED_LOWDELAY, nullptr);
		qWarning("AudioInput: Opus encoder set for low delay");
	} else if (iAudioQuality >= 32000) { // > 32 kbit/s bitrate
		opusState = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_AUDIO, nullptr);
		qWarning("AudioInput: Opus encoder set for high quality speech");
	} else {
		opusState = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, nullptr);
		qWarning("AudioInput: Opus encoder set for low quality speech");
	}

	opus_encoder_ctl(opusState, OPUS_SET_VBR(0)); // CBR

	initializeInputEnhancement();
	m_inputEnhancementProbationServiceTimer.setInterval(250);
	connect(&m_inputEnhancementProbationServiceTimer, &QTimer::timeout, this, [this]() {
		const Mumble::InputEnhancement::ProbationSettingsResult result = serviceInputEnhancementProbation();
		if (result == Mumble::InputEnhancement::ProbationSettingsResult::RolledBack) {
			qWarning("AudioInput: Input enhancement probation rolled back; Undo is available in Audio Input settings");
			// The persisted rollback is authoritative. Recreate capture on the
			// next main-loop turn so this AudioInput can return from its timer
			// callback before Audio::restartInput() destroys it. Startup then
			// verifies and prepares the exact last-known-good binding, falling
			// to Original if its signed asset is no longer available.
			QTimer::singleShot(0, []() { Audio::restartInput(); });
		} else if (result == Mumble::InputEnhancement::ProbationSettingsResult::MarkedHealthy) {
			qInfo("AudioInput: Input enhancement probation completed successfully");
		}
	});
	m_inputEnhancementProbationServiceTimer.start();
	m_webrtcEchoCanceller = std::make_unique< WebRTCAudioEchoCanceller >(iSampleRate, iFrameSize);

	qWarning("AudioInput: %d bits/s, %d hz, %d sample", iAudioQuality, iSampleRate, iFrameSize);
	iEchoFreq = iMicFreq = iSampleRate;

	iFrameCounter            = 0;
	iSilentFrames            = 0;
	iHoldFrames              = 0;
	m_inputGateOpen          = false;
	m_inputGateAttackFrames  = 0;
	m_inputGateReleaseFrames = 0;
	iBufferedFrames          = 0;

	bUserIsMuted = false;

	bResetProcessor = true;

	bEchoMulti = false;

	sesEcho = nullptr;
	srsMic = srsEcho = nullptr;

	iEchoChannels = iMicChannels = 0;
	iEchoFilled = iMicFilled = 0;
	eMicFormat = eEchoFormat = SampleFloat;
	iMicSampleSize = iEchoSampleSize = 0;

	bPreviousVoice = false;

	bResetEncoder = true;

	pfMicInput = pfEchoInput = nullptr;

	iBitrate    = 0;
	dPeakSignal = dPeakSpeaker = dPeakMic = dPeakCleanMic = 0.0;
	fSpeechProb = 0.0f;

	if (Global::get().uiSession) {
		setMaxBandwidth(Global::get().iMaxBandwidth);
	}

	bRunning = true;

	connect(this, SIGNAL(doDeaf()), Global::get().mw->qaAudioDeaf, SLOT(trigger()), Qt::QueuedConnection);
	connect(this, SIGNAL(doMute()), Global::get().mw->qaAudioMute, SLOT(trigger()), Qt::QueuedConnection);
	connect(this, &AudioInput::doMuteCue, Global::get().mw, &MainWindow::showMuteCuePopup);
}

AudioInput::~AudioInput() {
	m_inputEnhancementProbationServiceTimer.stop();
	bRunning = false;
	wait();
	m_inputEnhancementCalibrationForCallback.store(nullptr, std::memory_order_release);
	m_inputEnhancementCalibrationTransmissionBlock.endAfterCallbackQuiescence();
	if (m_inputEnhancementCalibrationRuntime) {
		m_inputEnhancementCalibrationRuntime->abort();
		m_inputEnhancementCalibrationRuntime.reset();
	}
	// The capture thread is quiescent, so Auto's lifecycle worker can now be
	// joined before any members captured by its factories are destroyed.
	m_inputEnhancementAutoPipelineBank.reset();
	serviceInputEnhancementProbation();

	if (opusState) {
		opus_encoder_destroy(opusState);
	}

	if (sesEcho)
		speex_echo_state_destroy(sesEcho);

	if (srsMic)
		speex_resampler_destroy(srsMic);
	if (srsEcho)
		speex_resampler_destroy(srsEcho);

	delete[] pfMicInput;
	delete[] pfEchoInput;
}

bool AudioInput::isTransmitting() const {
	return m_voiceActivityTransmitting.load(std::memory_order_relaxed);
}

AudioInput::VoiceActivitySnapshot AudioInput::voiceActivitySnapshot() const noexcept {
	VoiceActivitySnapshot snapshot;
	snapshot.peakSignalDb = m_voiceActivityPeakSignalDb.load(std::memory_order_relaxed);
	snapshot.peakCleanMicDb = m_voiceActivityPeakCleanMicDb.load(std::memory_order_relaxed);
	snapshot.speechProbability = m_voiceActivitySpeechProbability.load(std::memory_order_relaxed);
	snapshot.bitrate = m_voiceActivityBitrate.load(std::memory_order_relaxed);
	snapshot.transmitting = m_voiceActivityTransmitting.load(std::memory_order_relaxed);
	return snapshot;
}

bool AudioInput::inputEnhancementHealthyForUpdate() const noexcept {
	return m_inputEnhancementHealthyForUpdate.load(std::memory_order_relaxed);
}

std::optional< Mumble::InputEnhancement::RecipeBinding > AudioInput::healthyActiveInputEnhancementBinding(
	const Mumble::InputEnhancement::DeviceIdentity &identity,
	const Mumble::InputEnhancement::DefaultPreference &preference) const {
	using namespace Mumble::InputEnhancement;
	if (!canAuthorizeInputEnhancementRollbackBinding(
			inputEnhancementHealthyForUpdate(),
			m_inputEnhancementCaptureOpened.load(std::memory_order_acquire), isRunning())
		|| preference.profile == Profile::Original
		|| preference.profile == Profile::Auto || !deviceIdentitiesMatch(inputDeviceIdentity(), identity)
		|| !m_inputEnhancementCalibrationBaselineAvailable
		|| m_inputEnhancementCalibrationBaselinePreference != preference
		|| !m_inputEnhancementCalibrationBaselineBinding) {
		return std::nullopt;
	}
	return m_inputEnhancementCalibrationBaselineBinding;
}

bool AudioInput::startInputEnhancementCalibration(const Mumble::InputEnhancement::DefaultPreference &candidateControls,
												  bool captureOptionalLocalNoise, std::uint64_t blindSeed,
												  InputEnhancementCalibrationStartError *error) {
	auto reject = [error](InputEnhancementCalibrationStartError reason) {
		if (error) {
			*error = reason;
		}
		return false;
	};
	if (error) {
		*error = InputEnhancementCalibrationStartError::None;
	}
	if (m_inputEnhancementCalibrationRuntime && m_inputEnhancementCalibrationRuntime->transmissionBlocked()) {
		return reject(InputEnhancementCalibrationStartError::AlreadyRunning);
	}
	if (Mumble::InputEnhancement::runtimeAutoAdaptationEnabled(candidateControls)
		|| Mumble::InputEnhancement::runtimeAutoAdaptationEnabled(
			m_inputEnhancementCalibrationBaselinePreference)) {
		return reject(InputEnhancementCalibrationStartError::AutoUnsupported);
	}
	if (m_usesLegacyInputEnhancement) {
		return reject(InputEnhancementCalibrationStartError::LegacyOverrideActive);
	}
	if (!inputEnhancementHealthyForUpdate()) {
		return reject(InputEnhancementCalibrationStartError::ActiveRecipeUnhealthy);
	}
	if (!m_inputEnhancementCalibrationBaselineAvailable
		|| (m_inputEnhancementCalibrationBaselinePreference.profile != Mumble::InputEnhancement::Profile::Original
			&& !m_inputEnhancementCalibrationBaselineBinding)) {
		return reject(InputEnhancementCalibrationStartError::MissingExactActiveRecipe);
	}
	const Mumble::InputEnhancement::DeviceIdentity identity = inputDeviceIdentity();

	// Publish the independent final-encode gate before allocating, starting, or
	// publishing the capture bridge. A callback that already observed a null
	// bridge must still be unable to encode the frame.
	m_inputEnhancementCalibrationTransmissionBlock.begin();
	try {
		// Authorization includes the candidate control values, so rebuild it for
		// every terminal calibration attempt instead of retaining a stale draft.
		m_inputEnhancementCalibrationForCallback.store(nullptr, std::memory_order_release);
		m_inputEnhancementCalibrationRuntime.reset();
		Mumble::InputEnhancement::CalibrationOpusConfiguration opus;
		opus.bitrate                                                 = iAudioQuality;
		opus.framesPerPacket                                         = static_cast< unsigned int >(iAudioFrames);
		opus.allowLowDelay                                           = bAllowLowDelay;
		const Mumble::InputEnhancement::CpuClass calibrationCpuClass =
			currentInputEnhancementCpuClass(Mumble::InputEnhancement::Profile::Quality);

		Mumble::InputEnhancement::CalibrationPackageAuthorization authorization;
		const Mumble::InputEnhancement::InputEnhancementPackageVerifier *verifier =
			Global::get().inputEnhancementPackageVerifier;
		if (verifier && verifier->verificationHealthy()) {
			std::vector< Mumble::InputEnhancement::CalibrationPackageAuthorization::AuthorizedRecipe >
				authorizedRecipes;
			for (const Mumble::InputEnhancement::Profile profile :
				 { Mumble::InputEnhancement::Profile::Original, Mumble::InputEnhancement::Profile::Light,
				   Mumble::InputEnhancement::Profile::Balanced, Mumble::InputEnhancement::Profile::Quality,
				   Mumble::InputEnhancement::Profile::VoiceFocus }) {
				Mumble::InputEnhancement::ResolveRequest request;
				request.profile             = profile;
				request.noiseReduction      = candidateControls.reduction;
				request.naturalCrisp        = candidateControls.character;
				request.cpuClass            = calibrationCpuClass;
				request.backendAvailability = Mumble::InputEnhancement::BackendAvailability::compiled();
				request.captureDevice = Mumble::InputEnhancement::CaptureDeviceContext::liveDevice(
					identity.backendId, identity.stable);
				const Mumble::InputEnhancement::ProfileReadiness readiness = verifier->readinessForProfile(request);
				if (!readiness.selectable) {
					continue;
				}
				const Mumble::InputEnhancement::Recipe recipe =
					Mumble::InputEnhancement::RecipeCatalog::resolve(request);
				if (recipe.effectiveProfile() != profile || !verifier->recipeAuthorized(recipe)) {
					continue;
				}
				QString sha256;
				QString modelPath;
				if (recipe.usesNeuralProcessor()) {
					if (!verifier->modelAuthorized(recipe.modelId())) {
						continue;
					}
					sha256                          = verifier->modelSha256Hex(recipe.modelId());
					modelPath                       = verifier->modelPath(recipe.modelId());
					const QString relativeModelPath = verifier->modelRelativePath(recipe.modelId());
					authorizedRecipes.push_back(
						Mumble::InputEnhancement::CalibrationPackageAuthorization::authorizeRecipe(
							recipe, sha256, modelPath, relativeModelPath));
					continue;
				}
				authorizedRecipes.push_back(
					Mumble::InputEnhancement::CalibrationPackageAuthorization::authorizeRecipe(recipe, sha256, modelPath));
			}
			const QString catalogRevision =
				verifier->managedBySignedPackage() ? verifier->catalogRevision() : QStringLiteral("unmanaged-build-zero");
			authorization = Mumble::InputEnhancement::CalibrationPackageAuthorization::catalogBoundPackage(
				catalogRevision, std::move(authorizedRecipes));
		}
		auto evaluator = std::make_unique< Mumble::InputEnhancement::LocalCalibrationCandidateEvaluator >(
			opus, std::move(authorization), calibrationCpuClass,
			Mumble::InputEnhancement::CaptureDeviceContext::liveDevice(identity.backendId, identity.stable));
		m_inputEnhancementCalibrationRuntime =
			std::make_unique< Mumble::InputEnhancement::CalibrationRuntimeBridge >(std::move(evaluator),
																											 calibrationCpuClass);
	} catch (...) {
		synchronizeInputEnhancementCalibrationTransmissionBlock();
		throw;
	}

	const bool started = m_inputEnhancementCalibrationRuntime->start(
		identity, m_inputEnhancementCalibrationBaselinePreference, captureOptionalLocalNoise, blindSeed,
		m_inputEnhancementCalibrationBaselineBinding);
	if (started) {
		m_inputEnhancementCalibrationForCallback.store(m_inputEnhancementCalibrationRuntime.get(),
													   std::memory_order_release);
	} else {
		synchronizeInputEnhancementCalibrationTransmissionBlock();
		return reject(InputEnhancementCalibrationStartError::RuntimeRejected);
	}
	return true;
}

Mumble::InputEnhancement::CalibrationRuntimeBridge *AudioInput::inputEnhancementCalibrationRuntime() noexcept {
	return m_inputEnhancementCalibrationRuntime.get();
}

bool AudioInput::applyInputEnhancementCalibration(Mumble::InputEnhancement::Settings &settings,
																  qint64 nowEpochMs) {
	using namespace Mumble::InputEnhancement;
	CalibrationRuntimeBridge *runtime = inputEnhancementCalibrationRuntime();
	const DefaultPreference *draft    = runtime ? runtime->draftPreference() : nullptr;
	const RecipeBinding *binding      = runtime ? runtime->draftRecipeBinding() : nullptr;
	if (!runtime || !draft || !binding) {
		return false;
	}

	ResolveRequest request;
	request.profile             = draft->profile;
	request.noiseReduction      = draft->reduction;
	request.naturalCrisp        = draft->character;
	request.cpuClass            = currentInputEnhancementCpuClass(draft->profile);
	request.backendAvailability = BackendAvailability::compiled();
	const DeviceIdentity identity = inputDeviceIdentity();
	request.captureDevice = CaptureDeviceContext::liveDevice(identity.backendId, identity.stable);
	const Recipe recipe = RecipeCatalog::resolve(request);
	if (!profileReadiness(request).selectable || recipe.effectiveProfile() != draft->profile) {
		return false;
	}

	// Original does not depend on a package and remains a safe calibration exit.
	// Every enhanced profile is rebound to the currently verified package so a
	// catalog/model change during blind listening cannot persist a stale choice.
	if (draft->profile != Profile::Original) {
		const InputEnhancementPackageVerifier *verifier = Global::get().inputEnhancementPackageVerifier;
		if (!verifier || !verifier->verificationHealthy() || !verifier->readinessForProfile(request).selectable
			|| !verifier->recipeAuthorized(recipe)) {
			return false;
		}
		QString modelSha256;
		QString modelRelativePath;
		if (recipe.usesNeuralProcessor()) {
			if (!verifier->modelAuthorized(recipe.modelId())) {
				return false;
			}
			modelSha256       = verifier->modelSha256Hex(recipe.modelId());
			modelRelativePath = verifier->modelRelativePath(recipe.modelId());
		}
		const QString catalogRevision = verifier->managedBySignedPackage()
			? verifier->catalogRevision()
			: QStringLiteral("unmanaged-build-zero");
		if (!recipeBindingMatches(*binding, recipe, catalogRevision, modelSha256, modelRelativePath)) {
			return false;
		}
	}

	return runtime->apply(settings, nowEpochMs);
}

void AudioInput::synchronizeInputEnhancementCalibrationTransmissionBlock() noexcept {
	if (m_inputEnhancementCalibrationRuntime && m_inputEnhancementCalibrationRuntime->transmissionBlocked()) {
		return;
	}
	// Terminal runtime transitions first pause/wait for appendPcmFromCallback().
	// Only then is the bridge unpublished and the independent encode gate opened.
	m_inputEnhancementCalibrationForCallback.store(nullptr, std::memory_order_release);
	m_inputEnhancementCalibrationTransmissionBlock.endAfterCallbackQuiescence();
}

Mumble::InputEnhancement::ProbationSettingsResult AudioInput::serviceInputEnhancementProbation() {
	const Mumble::InputEnhancement::ProbationSettingsResult result =
		m_inputEnhancementProbation.serviceSettings(Global::get().s.inputEnhancement);
	if (result != Mumble::InputEnhancement::ProbationSettingsResult::None) {
		// Mark-healthy and rollback are safety decisions, not transient UI
		// state. Persist them before another callback/session can observe the
		// resolved probation as complete.
		try {
			Global::get().s.save();
		} catch (const std::exception &exception) {
			qWarning("AudioInput: Unable to persist input enhancement probation result: %s", exception.what());
		} catch (...) {
			qWarning("AudioInput: Unable to persist input enhancement probation result");
		}
	}
	return result;
}

bool AudioInput::undoInputEnhancementProbationRollback(Mumble::InputEnhancement::Settings &settings) {
	return m_inputEnhancementProbation.undoRollback(settings);
}

bool AudioInput::inputEnhancementProbationRunning() const noexcept {
	return m_inputEnhancementProbation.running();
}

bool AudioInput::inputEnhancementProbationUndoAvailable() const noexcept {
	return m_inputEnhancementProbation.undoAvailable();
}

#define IN_MIXER_FLOAT(channels)                                                                             \
	static void inMixerFloat##channels(float *RESTRICT buffer, const void *RESTRICT ipt, unsigned int nsamp, \
									   unsigned int N, quint64 mask) {                                       \
		const float *RESTRICT input = reinterpret_cast< const float * >(ipt);                                \
		const float m               = 1.0f / static_cast< float >(channels);                                 \
		Q_UNUSED(N);                                                                                         \
		Q_UNUSED(mask);                                                                                      \
		for (unsigned int i = 0; i < nsamp; ++i) {                                                           \
			float v = 0.0f;                                                                                  \
			for (unsigned int j = 0; j < channels; ++j)                                                      \
				v += input[i * channels + j];                                                                \
			buffer[i] = v * m;                                                                               \
		}                                                                                                    \
	}

#define IN_MIXER_SHORT(channels)                                                                             \
	static void inMixerShort##channels(float *RESTRICT buffer, const void *RESTRICT ipt, unsigned int nsamp, \
									   unsigned int N, quint64 mask) {                                       \
		const short *RESTRICT input = reinterpret_cast< const short * >(ipt);                                \
		const float m               = 1.0f / (32768.f * static_cast< float >(channels));                     \
		Q_UNUSED(N);                                                                                         \
		Q_UNUSED(mask);                                                                                      \
		for (unsigned int i = 0; i < nsamp; ++i) {                                                           \
			float v = 0.0f;                                                                                  \
			for (unsigned int j = 0; j < channels; ++j)                                                      \
				v += static_cast< float >(input[i * channels + j]);                                          \
			buffer[i] = v * m;                                                                               \
		}                                                                                                    \
	}

static void inMixerFloatMask(float *RESTRICT buffer, const void *RESTRICT ipt, unsigned int nsamp, unsigned int N,
							 quint64 mask) {
	const float *RESTRICT input = reinterpret_cast< const float * >(ipt);

	unsigned int chancount = 0;
	static std::vector< unsigned int > chanindex;
	chanindex.resize(N);
	for (unsigned int j = 0; j < N; ++j) {
		if ((mask & (1ULL << j)) == 0) {
			continue;
		}
		chanindex[chancount] = j; // Use chancount as index into chanindex.
		++chancount;
	}

	const float m = 1.0f / static_cast< float >(chancount);
	for (unsigned int i = 0; i < nsamp; ++i) {
		float v = 0.0f;
		for (unsigned int j = 0; j < chancount; ++j) {
			v += input[i * N + chanindex[j]];
		}
		buffer[i] = v * m;
	}
}

static void inMixerShortMask(float *RESTRICT buffer, const void *RESTRICT ipt, unsigned int nsamp, unsigned int N,
							 quint64 mask) {
	const short *RESTRICT input = reinterpret_cast< const short * >(ipt);

	unsigned int chancount = 0;
	static std::vector< unsigned int > chanindex;
	chanindex.resize(N);
	for (unsigned int j = 0; j < N; ++j) {
		if ((mask & (1ULL << j)) == 0) {
			continue;
		}
		chanindex[chancount] = j; // Use chancount as index into chanindex.
		++chancount;
	}

	const float m = 1.0f / static_cast< float >(chancount);
	for (unsigned int i = 0; i < nsamp; ++i) {
		float v = 0.0f;
		for (unsigned int j = 0; j < chancount; ++j) {
			v += static_cast< float >(input[i * N + chanindex[j]]);
		}
		buffer[i] = v * m;
	}
}

IN_MIXER_FLOAT(1)
IN_MIXER_FLOAT(2)
IN_MIXER_FLOAT(3)
IN_MIXER_FLOAT(4)
IN_MIXER_FLOAT(5)
IN_MIXER_FLOAT(6)
IN_MIXER_FLOAT(7)
IN_MIXER_FLOAT(8)
IN_MIXER_FLOAT(N)

IN_MIXER_SHORT(1)
IN_MIXER_SHORT(2)
IN_MIXER_SHORT(3)
IN_MIXER_SHORT(4)
IN_MIXER_SHORT(5)
IN_MIXER_SHORT(6)
IN_MIXER_SHORT(7)
IN_MIXER_SHORT(8)
IN_MIXER_SHORT(N)

#undef IN_MIXER_FLOAT
#undef IN_MIXER_SHORT

AudioInput::inMixerFunc AudioInput::chooseMixer(const unsigned int nchan, SampleFormat sf, quint64 chanmask) {
	inMixerFunc r = nullptr;

	if (chanmask != 0xffffffffffffffffULL) {
		if (sf == SampleFloat) {
			r = inMixerFloatMask;
		} else if (sf == SampleShort) {
			r = inMixerShortMask;
		}
		return r;
	}

	if (sf == SampleFloat) {
		switch (nchan) {
			case 1:
				r = inMixerFloat1;
				break;
			case 2:
				r = inMixerFloat2;
				break;
			case 3:
				r = inMixerFloat3;
				break;
			case 4:
				r = inMixerFloat4;
				break;
			case 5:
				r = inMixerFloat5;
				break;
			case 6:
				r = inMixerFloat6;
				break;
			case 7:
				r = inMixerFloat7;
				break;
			case 8:
				r = inMixerFloat8;
				break;
			default:
				r = inMixerFloatN;
				break;
		}
	} else {
		switch (nchan) {
			case 1:
				r = inMixerShort1;
				break;
			case 2:
				r = inMixerShort2;
				break;
			case 3:
				r = inMixerShort3;
				break;
			case 4:
				r = inMixerShort4;
				break;
			case 5:
				r = inMixerShort5;
				break;
			case 6:
				r = inMixerShort6;
				break;
			case 7:
				r = inMixerShort7;
				break;
			case 8:
				r = inMixerShort8;
				break;
			default:
				r = inMixerShortN;
				break;
		}
	}
	return r;
}

void AudioInput::initializeMixer() {
	int err;

	if (srsMic)
		speex_resampler_destroy(srsMic);
	if (srsEcho)
		speex_resampler_destroy(srsEcho);
	delete[] pfMicInput;
	delete[] pfEchoInput;

	if (iMicFreq != iSampleRate)
		srsMic = speex_resampler_init(1, iMicFreq, iSampleRate, 3, &err);

	iMicLength = (iFrameSize * iMicFreq) / iSampleRate;

	pfMicInput = new float[iMicLength];

	if (iEchoChannels > 0) {
		bEchoMulti = (Global::get().s.echoOption == EchoCancelOptionID::SPEEX_MULTICHANNEL);
		if (iEchoFreq != iSampleRate)
			srsEcho = speex_resampler_init(bEchoMulti ? iEchoChannels : 1, iEchoFreq, iSampleRate, 3, &err);
		iEchoLength    = (iFrameSize * iEchoFreq) / iSampleRate;
		iEchoMCLength  = bEchoMulti ? iEchoLength * iEchoChannels : iEchoLength;
		iEchoFrameSize = bEchoMulti ? iFrameSize * iEchoChannels : iFrameSize;
		pfEchoInput    = new float[iEchoMCLength];
	} else {
		srsEcho     = nullptr;
		pfEchoInput = nullptr;
	}

	uiMicChannelMask = Global::get().s.uiAudioInputChannelMask;

	// There is no channel mask setting for the echo canceller, so allow all channels.
	uiEchoChannelMask = 0xffffffffffffffffULL;

	imfMic  = chooseMixer(iMicChannels, eMicFormat, uiMicChannelMask);
	imfEcho = chooseMixer(iEchoChannels, eEchoFormat, uiEchoChannelMask);

	iMicSampleSize =
		static_cast< unsigned int >(iMicChannels * ((eMicFormat == SampleFloat) ? sizeof(float) : sizeof(short)));
	iEchoSampleSize =
		static_cast< unsigned int >(iEchoChannels * ((eEchoFormat == SampleFloat) ? sizeof(float) : sizeof(short)));

	bResetProcessor = true;

	qWarning("AudioInput: Initialized mixer for %d channel %d hz mic and %d channel %d hz echo", iMicChannels, iMicFreq,
			 iEchoChannels, iEchoFreq);
	if (uiMicChannelMask != 0xffffffffffffffffULL) {
		qWarning("AudioInput: using mic channel mask 0x%llx", static_cast< unsigned long long >(uiMicChannelMask));
	}
}

void AudioInput::addMic(const void *data, unsigned int nsamp) {
	while (nsamp > 0) {
		// Make sure we don't overrun the frame buffer
		const unsigned int left = qMin(nsamp, iMicLength - iMicFilled);

		// Append mix into pfMicInput frame buffer (converts 16bit pcm->float if necessary)
		imfMic(pfMicInput + iMicFilled, data, left, iMicChannels, uiMicChannelMask);

		iMicFilled += left;
		nsamp -= left;

		// If new samples are left offset data pointer to point at the first one for next iteration
		if (nsamp > 0) {
			if (eMicFormat == SampleFloat)
				data = reinterpret_cast< const float * >(data) + left * iMicChannels;
			else
				data = reinterpret_cast< const short * >(data) + left * iMicChannels;
		}

		if (iMicFilled == iMicLength) {
			// Frame complete
			iMicFilled = 0;

			// If needed resample frame
			float *pfOutput = srsMic ? (float *) alloca(iFrameSize * sizeof(float)) : nullptr;
			float *ptr      = srsMic ? pfOutput : pfMicInput;

			if (srsMic) {
				spx_uint32_t inlen  = iMicLength;
				spx_uint32_t outlen = iFrameSize;
				speex_resampler_process_float(srsMic, 0, pfMicInput, &inlen, pfOutput, &outlen);
			}

			// If echo cancellation is enabled the pointer ends up in the resynchronizer queue
			// and may need to outlive this function's frame
			short *psMic = iEchoChannels > 0 ? new short[iFrameSize] : (short *) alloca(iFrameSize * sizeof(short));

			// Convert float to 16bit PCM
			const float mul = 32768.f;
			for (int j = 0; j < iFrameSize; ++j)
				psMic[j] = static_cast< short >(qBound(-32768.f, (ptr[j] * mul), 32767.f));

			// If we have echo cancellation enabled...
			if (iEchoChannels > 0) {
				resync.addMic(psMic);
			} else {
				encodeAudioFrame(AudioChunk(psMic));
			}
		}
	}
}

void AudioInput::addEcho(const void *data, unsigned int nsamp) {
	while (nsamp > 0) {
		// Make sure we don't overrun the echo frame buffer
		const unsigned int left = qMin(nsamp, iEchoLength - iEchoFilled);

		if (bEchoMulti) {
			const unsigned int samples = left * iEchoChannels;

			if (eEchoFormat == SampleFloat) {
				for (unsigned int i = 0; i < samples; ++i)
					pfEchoInput[i + iEchoFilled * iEchoChannels] = reinterpret_cast< const float * >(data)[i];
			} else {
				// 16bit PCM -> float
				for (unsigned int i = 0; i < samples; ++i)
					pfEchoInput[i + iEchoFilled * iEchoChannels] =
						static_cast< float >(reinterpret_cast< const short * >(data)[i]) * (1.0f / 32768.f);
			}
		} else {
			// Mix echo channels (converts 16bit PCM -> float if needed)
			imfEcho(pfEchoInput + iEchoFilled, data, left, iEchoChannels, uiEchoChannelMask);
		}

		iEchoFilled += left;
		nsamp -= left;

		// If new samples are left offset data pointer to point at the first one for next iteration
		if (nsamp > 0) {
			if (eEchoFormat == SampleFloat)
				data = reinterpret_cast< const float * >(data) + left * iEchoChannels;
			else
				data = reinterpret_cast< const short * >(data) + left * iEchoChannels;
		}

		if (iEchoFilled == iEchoLength) {
			// Frame complete

			iEchoFilled = 0;

			// Resample if necessary
			float *pfOutput = srsEcho ? (float *) alloca(iEchoFrameSize * sizeof(float)) : nullptr;
			float *ptr      = srsEcho ? pfOutput : pfEchoInput;

			if (srsEcho) {
				spx_uint32_t inlen  = iEchoLength;
				spx_uint32_t outlen = iFrameSize;
				speex_resampler_process_interleaved_float(srsEcho, pfEchoInput, &inlen, pfOutput, &outlen);
			}

			short *outbuff = new short[iEchoFrameSize];

			// float -> 16bit PCM
			const float mul = 32768.f;
			for (unsigned int j = 0; j < iEchoFrameSize; ++j) {
				outbuff[j] = static_cast< short >(qBound(-32768.f, (ptr[j] * mul), 32767.f));
			}

			auto chunk = resync.addSpeaker(outbuff);
			if (!chunk.empty()) {
				encodeAudioFrame(chunk);
				delete[] chunk.mic;
				delete[] chunk.speaker;
			}
		}
	}
}

int AudioInput::clampFramesPerPacket(int frames) {
	return std::clamp(frames, 1, 6);
}

int AudioInput::packetDurationMsForFrames(int frames) {
	return clampFramesPerPacket(frames) * 10;
}

int AudioInput::opusMaxAudioBitrateForFrames(int frames) {
	const int packetDurationMs = packetDurationMsForFrames(frames);
	// Keep room for the largest server-to-client voice wrapper so packets from this client remain relayable to
	// both legacy and protobuf clients.
	constexpr int maxAudioPacketOverhead = 64;
	static_assert(Mumble::Protocol::MAX_UDP_PACKET_SIZE > maxAudioPacketOverhead);
	const int maxPayloadBytes =
		std::min(1275, static_cast< int >(Mumble::Protocol::MAX_UDP_PACKET_SIZE) - maxAudioPacketOverhead);
	const int packetCap = static_cast< int >((maxPayloadBytes * 8 * 1000) / packetDurationMs);

	return std::min(packetCap, 510000);
}

int AudioInput::maxAudioBitrateForConfiguration(int serverBandwidth, int frames, bool experimentalHighBitrateEnabled,
												bool transmitPosition, bool tcpMode) {
	const int clampedFrames = clampFramesPerPacket(frames);
	const int packetCap     = opusMaxAudioBitrateForFrames(clampedFrames);
	const int configuredCap = experimentalHighBitrateEnabled ? 510000 : 192000;
	const int hardCap       = std::min(configuredCap, packetCap);
	if (serverBandwidth == -1) {
		return std::max(8000, hardCap);
	}

	const int headerBytes = 20 + 8 + 4 + 1 + 2 + (transmitPosition ? 12 : 0) + (tcpMode ? 12 : 0) + clampedFrames;
	const int overhead    = headerBytes * (800 / clampedFrames);

	return std::clamp(serverBandwidth - overhead, 8000, hardCap);
}

void AudioInput::adjustBandwidth(int bitspersec, int &bitrate, int &frames, bool &allowLowDelay) {
	frames        = clampFramesPerPacket(Global::get().s.iFramesPerPacket);
	bitrate       = Global::get().s.iQuality;
	allowLowDelay = Global::get().s.bAllowLowDelay;

	const int maxBitrate =
		maxAudioBitrateForConfiguration(bitspersec, frames, Global::get().s.experimentalHighBitrateEnabled,
										Global::get().s.bTransmitPosition, NetworkConfig::TcpModeEnabled());

	bitrate = std::clamp(bitrate, 8000, maxBitrate);
}

void AudioInput::setMaxBandwidth(int bitspersec) {
	if (bitspersec == Global::get().iMaxBandwidth)
		return;

	int frames;
	int bitrate;
	bool allowLowDelay;
	adjustBandwidth(bitspersec, bitrate, frames, allowLowDelay);

	Global::get().iMaxBandwidth = bitspersec;

	if (bitspersec != -1) {
		if ((bitrate != Global::get().s.iQuality) || (frames != Global::get().s.iFramesPerPacket))
			Global::get().mw->msgBox(
				tr("Server maximum network bandwidth is only %1 kbit/s. Audio quality auto-adjusted to %2 "
				   "kbit/s (%3 ms)")
					.arg(bitspersec / 1000)
					.arg(bitrate / 1000)
					.arg(frames * 10));
	}

	AudioInputPtr ai = Global::get().ai;
	if (ai) {
		Global::get().iAudioBandwidth = getNetworkBandwidth(bitrate, frames);
		ai->iAudioQuality             = bitrate;
		ai->iAudioFrames              = frames;
		ai->bAllowLowDelay            = allowLowDelay;
		return;
	}

	ai.reset();

	Audio::stopInput();
	Audio::startInput();
}

int AudioInput::getNetworkBandwidth(int bitrate, int frames) {
	frames       = clampFramesPerPacket(frames);
	int overhead = 20 + 8 + 4 + 1 + 2 + (Global::get().s.bTransmitPosition ? 12 : 0)
				   + (NetworkConfig::TcpModeEnabled() ? 12 : 0) + frames;
	overhead *= (800 / frames);
	int bw = overhead + bitrate;

	return bw;
}

void AudioInput::resetAudioProcessor() {
	if (!bResetProcessor)
		return;

	if (sesEcho)
		speex_echo_state_destroy(sesEcho);

	m_preprocessor.init(iSampleRate, iFrameSize);
	resync.reset();
	m_inputGateOpen          = false;
	m_inputGateAttackFrames  = 0;
	m_inputGateReleaseFrames = 0;
	m_speechCleanupTransmitDrain.cancel();
	selectNoiseCancel();

	m_preprocessor.setVAD(true);
	m_preprocessor.setAGC(true);
	m_preprocessor.setDereverb(true);

	m_preprocessor.setAGCTarget(30000);

	const int minLoudness = std::clamp(Global::get().s.iMinLoudness, 500, 30000);
	const float v         = 30000.0f / static_cast< float >(minLoudness);
	m_preprocessor.setAGCMaxGain(static_cast< std::int32_t >(floorf(20.0f * log10f(v))));
	m_preprocessor.setAGCDecrement(-60);

	if (m_usesLegacyInputEnhancement
		&& (noiseCancel == Settings::NoiseCancelSpeex || noiseCancel == Settings::NoiseCancelBoth)) {
		m_preprocessor.setNoiseSuppress(m_inputEnhancementSpeexStrength);
	}

	if (iEchoChannels > 0) {
		const bool requestedWebRTCAec = (Global::get().s.echoOption == EchoCancelOptionID::WEBRTC_AEC);
		const bool useWebRTCAec       = requestedWebRTCAec && m_webrtcEchoCanceller && m_webrtcEchoCanceller->isReady();

		if (m_webrtcEchoCanceller) {
			m_webrtcEchoCanceller->reset();
		}

		if (useWebRTCAec) {
			sesEcho = nullptr;
			m_preprocessor.setEchoState(nullptr);
			qWarning("AudioInput: WEBRTC ECHO CANCELLER ACTIVE");
		} else {
			if (requestedWebRTCAec) {
				qWarning("AudioInput: WebRTC echo canceller requested but unavailable, falling back to Speex");
			}

			int filterSize = iFrameSize * (10 + resync.getNominalLag());
			sesEcho =
				speex_echo_state_init_mc(iFrameSize, filterSize, 1, bEchoMulti ? static_cast< int >(iEchoChannels) : 1);
			int iArg = iSampleRate;
			speex_echo_ctl(sesEcho, SPEEX_ECHO_SET_SAMPLING_RATE, &iArg);
			m_preprocessor.setEchoState(sesEcho);

			qWarning("AudioInput: SPEEX ECHO CANCELLER ACTIVE");
		}
	} else {
		sesEcho = nullptr;
		m_preprocessor.setEchoState(nullptr);
	}

	bResetEncoder = true;

	bResetProcessor = false;
}

bool AudioInput::selectCodec() {
	// We only ever use Opus
	Mumble::Protocol::AudioCodec previousCodec = m_codec;

	assert(previousCodec == Mumble::Protocol::AudioCodec::Opus);

	m_codec = Mumble::Protocol::AudioCodec::Opus;

	if (m_codec != previousCodec) {
		iBufferedFrames = 0;
		qlFrames.clear();
		opusBuffer.clear();
	}

	return true;
}

void AudioInput::initializeInputEnhancement() {
	m_inputEnhancementHealthyForUpdate.store(true, std::memory_order_relaxed);
	m_inputEnhancementCaptureOpened.store(false, std::memory_order_relaxed);
	m_inputEnhancementCalibrationBaselinePreference = {};
	m_inputEnhancementCalibrationBaselineBinding.reset();
	m_inputEnhancementCalibrationBaselineAvailable = false;
	m_inputEnhancementAutoPipelineBank.reset();
	m_inputEnhancementAutoAdapt            = false;
	m_inputEnhancementAutoProfileSwitching = false;
	m_inputEnhancementAutoTracker.reset();
	m_inputEnhancementAutoSilenceBoundary.reset();
	m_inputEnhancementAutoPolicy.reset(Mumble::InputEnhancement::Profile::Balanced);
	m_inputEnhancementAutoSwitchGate.reset(Mumble::InputEnhancement::Profile::Balanced);
	m_inputEnhancementConcreteProfile                  = Mumble::InputEnhancement::Profile::Original;
	m_inputEnhancementLastWorkingProfile               = Mumble::InputEnhancement::Profile::Original;
	m_inputEnhancementRuntimeRecoveryPending           = false;
	const Mumble::InputEnhancement::Settings &settings = Global::get().s.inputEnhancement;
	const Mumble::InputEnhancement::DeviceProfileState *deviceProfile =
		Mumble::InputEnhancement::findDeviceProfile(settings, m_inputDeviceIdentity);
	const Mumble::InputEnhancement::DefaultPreference &preference =
		Mumble::InputEnhancement::preferenceForDevice(settings, m_inputDeviceIdentity);
	const bool unsafePendingState = deviceProfile && deviceProfile->pendingValidation
									&& (!deviceProfile->lastKnownGood
										|| !Mumble::InputEnhancement::executionBindingMatchesPreference(
											deviceProfile->preference, deviceProfile->pendingRecipeBinding,
											deviceProfile->pendingAutoRecipeSetFingerprint)
										|| !Mumble::InputEnhancement::executionBindingMatchesPreference(
											*deviceProfile->lastKnownGood, deviceProfile->lastKnownGoodRecipeBinding,
											deviceProfile->lastKnownGoodAutoRecipeSetFingerprint));
	const Mumble::InputEnhancement::RecipeBinding *expectedRecipeBinding = nullptr;
	if (deviceProfile && !unsafePendingState) {
		if (deviceProfile->pendingValidation && deviceProfile->pendingRecipeBinding) {
			expectedRecipeBinding = &*deviceProfile->pendingRecipeBinding;
		} else if (deviceProfile->lastKnownGood && deviceProfile->lastKnownGoodRecipeBinding
				   && deviceProfile->preference == *deviceProfile->lastKnownGood) {
			expectedRecipeBinding = &*deviceProfile->lastKnownGoodRecipeBinding;
		}
	}
	if (Global::get().bDisableInputEnhancement) {
		m_usesLegacyInputEnhancement = false;
		m_inputEnhancementEngine     = Mumble::InputEnhancement::Engine::None;
		m_inputEnhancementLightProcessor.reset();
		m_inputEnhancementPipeline.reset();
		m_inputEnhancementAutoPipelineBank.reset();
		m_speechCleanupProcessor.reset();
		m_configuredLegacyNoiseCancelMode = Settings::NoiseCancelOff;
		const Mumble::InputEnhancement::InputEnhancementPolicyController *policyController =
			Global::get().inputEnhancementPolicyController;
		const bool safePolicyOriginal =
			policyController
			&& policyController->policyForcedOriginalCanQualifyAudioHealth(
				Global::get().bInputEnhancementRecoveryDisabled);
		if (!safePolicyOriginal) {
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		}
		return;
	}
	// A policy-forced Original session deliberately leaves pending profile
	// probation untouched. If policy later re-enables enhancement, capture is
	// restarted and the exact candidate begins probation on its real audio path.
	initializeInputEnhancementProbation(deviceProfile);
	m_inputEnhancementBaseReduction                                = std::clamp(preference.reduction, 0, 100);
	m_inputEnhancementBaseCharacter                                = std::clamp(preference.character, 0, 100);
	m_inputEnhancementActiveReduction                              = m_inputEnhancementBaseReduction;
	m_inputEnhancementActiveCharacter                              = m_inputEnhancementBaseCharacter;
	const Mumble::InputEnhancement::LegacyOverride *legacyOverride = nullptr;
	if (deviceProfile && !unsafePendingState) {
		legacyOverride = deviceProfile->legacyOverride ? &*deviceProfile->legacyOverride : nullptr;
	} else if (settings.legacyOverride) {
		// A malformed pending per-device entry must not escape Original via a
		// global migration override.
		legacyOverride = unsafePendingState ? nullptr : &*settings.legacyOverride;
	}
	// Old settings files can retain a legacy override after their processing mode
	// has been switched off. Treat that dormant record as exact Original instead
	// of making the otherwise unmodified capture path look like an active legacy
	// recipe. Besides reflecting the actual audio path, this keeps local input
	// calibration available without forcing the user through a settings rewrite.
	const bool legacyOverrideProcessesAudio =
		legacyOverride && Mumble::InputEnhancement::legacyOverrideProcessingEnabled(*legacyOverride);
	if (!legacyOverrideProcessesAudio) {
		legacyOverride = nullptr;
	}
	m_usesLegacyInputEnhancement = legacyOverride != nullptr;
	m_inputEnhancementEngine     = Mumble::InputEnhancement::Engine::None;
	m_inputEnhancementLightProcessor.reset();
	m_inputEnhancementPipeline.reset();
	m_inputEnhancementAutoPipelineBank.reset();
	m_speechCleanupProcessor.reset();

	if (m_usesLegacyInputEnhancement) {
		const Mumble::InputEnhancement::LegacyOverride &legacy = *legacyOverride;
		m_inputEnhancementSpeexStrength                        = legacy.speexNoiseCancelStrength;
		m_configuredLegacyNoiseCancelMode = static_cast< Settings::NoiseCancel >(legacy.noiseCancelMode);
		m_speechCleanupSelection          = Mumble::SpeechCleanup::normalizeSelection(
            { static_cast< Settings::SpeechCleanupBackend >(legacy.backend), legacy.modelId, legacy.customModelPath });
		const Settings::NoiseCancel legacyMode = static_cast< Settings::NoiseCancel >(legacy.noiseCancelMode);
		if (legacyMode == Settings::NoiseCancelRNN || legacyMode == Settings::NoiseCancelBoth) {
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
			++m_speechCleanupE2EModelInitializationAttempts;
#endif
			m_speechCleanupProcessor = createSpeechCleanupProcessor(m_speechCleanupSelection);
			if (!m_speechCleanupProcessor || !m_speechCleanupProcessor->isReady()
				|| m_speechCleanupProcessor->usedFallback()) {
				m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
				failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
			}
		}
		return;
	}
	if (preference.profile == Mumble::InputEnhancement::Profile::Original) {
		// Original is the exact established Mumble path: it needs no signed
		// catalog, processor, model preparation or additional latency.
		m_configuredLegacyNoiseCancelMode               = Settings::NoiseCancelOff;
		m_inputEnhancementCalibrationBaselinePreference = preference;
		m_inputEnhancementCalibrationBaselineAvailable  = true;
		return;
	}
	if (preference.profile == Mumble::InputEnhancement::Profile::Auto) {
		// AutoV2's transition coordinator is callback-safe, but the current
		// PreparedPipelineBank API retires the source as soon as switchTo() is
		// called and exposes only one active Pipeline*. Until the bank can lease
		// stable source+candidate processors through commit/abort (including the
		// non-neural Light path), a true aligned dual-pipeline crossfade cannot be
		// wired without risking use-after-retire or an audible hard switch.
		qWarning("AudioInput: Auto runtime unavailable: PreparedPipelineBank cannot lease source and candidate "
				 "pipelines through an aligned transition; using Original");
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
		return;
	}

	// Signed package catalogs govern only the new product pipeline. Hidden
	// legacy overrides (including an explicit custom model path) deliberately
	// remain on the exact migration-preserved path above.
	const Mumble::InputEnhancement::InputEnhancementPackageVerifier *packageVerifier =
		Global::get().inputEnhancementPackageVerifier;
	if (!packageVerifier || !packageVerifier->verificationHealthy()) {
		qWarning("AudioInput: Signed input enhancement package catalog is unavailable; using Original");
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
		return;
	}

	Mumble::InputEnhancement::ResolveRequest request;
	request.profile             = preference.profile;
	request.noiseReduction      = preference.reduction;
	request.naturalCrisp        = preference.character;
	request.cpuClass            = currentInputEnhancementCpuClass(preference.profile);
	request.backendAvailability = Mumble::InputEnhancement::BackendAvailability::compiled();
	request.captureDevice       = Mumble::InputEnhancement::CaptureDeviceContext::liveDevice(
		m_inputDeviceIdentity.backendId, m_inputDeviceIdentity.stable);
	m_inputEnhancementCpuClass  = request.cpuClass;
	// Fixed-profile adaptation is intentionally dormant in the core community
	// release. Preserve its settings bit, but never let it alter qualified fixed
	// recipes until that path has independent quality evidence.
	m_inputEnhancementAutoAdapt = Mumble::InputEnhancement::runtimeAutoAdaptationEnabled(preference);
	m_inputEnhancementAutoProfileSwitching =
		m_inputEnhancementAutoAdapt && preference.profile == Mumble::InputEnhancement::Profile::Auto;
	m_inputEnhancementSpeexStrength =
		Mumble::InputEnhancement::lightSpeexSuppressionDb(preference.reduction);
	const Mumble::InputEnhancement::ProfileReadiness readiness = packageVerifier->readinessForProfile(request);
	if (!readiness.selectable) {
		qWarning("AudioInput: Requested input enhancement profile failed preflight (reason=%d); using Original",
				 static_cast< int >(readiness.reason));
		m_inputEnhancementAutoAdapt            = false;
		m_inputEnhancementAutoProfileSwitching = false;
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
		return;
	}
	if (!readiness.productionQualified) {
		qInfo("AudioInput: Input enhancement is running in preview/session-only mode for capture backend %s",
			  qUtf8Printable(request.captureDevice.backendId));
	}
	const Mumble::InputEnhancement::Recipe recipe = Mumble::InputEnhancement::RecipeCatalog::resolve(request);
	if (!packageVerifier->recipeAuthorized(recipe)) {
		qWarning("AudioInput: Input enhancement recipe %s is not authorized by the signed package; using Original",
				 qUtf8Printable(recipe.id()));
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
		return;
	}
	QString verifiedModelSha256;
	QString verifiedModelPath;
	QString verifiedModelRelativePath;
	if (recipe.usesNeuralProcessor()) {
		if (!packageVerifier->modelAuthorized(recipe.modelId())) {
			qWarning("AudioInput: Input enhancement model %s is not authorized by the signed package; using Original",
					 qUtf8Printable(recipe.modelId()));
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
			return;
		}
		verifiedModelSha256       = packageVerifier->modelSha256Hex(recipe.modelId());
		verifiedModelPath         = packageVerifier->modelPath(recipe.modelId());
		verifiedModelRelativePath = packageVerifier->modelRelativePath(recipe.modelId());
	}
	if (expectedRecipeBinding
		&& !Mumble::InputEnhancement::recipeBindingMatches(*expectedRecipeBinding, recipe,
														   packageVerifier->managedBySignedPackage()
															   ? packageVerifier->catalogRevision()
															   : QStringLiteral("unmanaged-build-zero"),
														   verifiedModelSha256, verifiedModelRelativePath)) {
		qWarning("AudioInput: Persisted exact input-enhancement recipe binding drifted; using Original");
		m_inputEnhancementAutoAdapt            = false;
		m_inputEnhancementAutoProfileSwitching = false;
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
		return;
	}
	if (preference.profile != Mumble::InputEnhancement::Profile::Auto
		&& recipe.effectiveProfile() != preference.profile) {
		qWarning("AudioInput: Requested input enhancement profile is unavailable; using Original");
		m_inputEnhancementAutoAdapt            = false;
		m_inputEnhancementAutoProfileSwitching = false;
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
		return;
	}
	std::optional< Mumble::InputEnhancement::RecipeBinding > activeExactRecipeBinding;
	if (preference.profile != Mumble::InputEnhancement::Profile::Auto) {
		activeExactRecipeBinding = Mumble::InputEnhancement::recipeBindingForRecipe(
			recipe,
			packageVerifier->managedBySignedPackage() ? packageVerifier->catalogRevision()
													  : QStringLiteral("unmanaged-build-zero"),
			verifiedModelSha256, verifiedModelRelativePath);
		if (!Mumble::InputEnhancement::isValidRecipeBinding(*activeExactRecipeBinding)) {
			qWarning("AudioInput: Resolved input enhancement recipe has no valid exact binding; using Original");
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
			return;
		}
	}
	m_inputEnhancementEngine          = recipe.engine();
	m_inputEnhancementConcreteProfile = recipe.effectiveProfile();
	m_inputEnhancementAutoPolicy.reset(m_inputEnhancementConcreteProfile);
	m_inputEnhancementAutoSwitchGate.reset(m_inputEnhancementConcreteProfile);

	switch (recipe.engine()) {
		case Mumble::InputEnhancement::Engine::RNNoise:
			m_speechCleanupSelection.backend = Settings::RNNoiseBackend;
			break;
		case Mumble::InputEnhancement::Engine::DeepFilterNet:
			m_speechCleanupSelection.backend = Settings::DeepFilterNetBackend;
			break;
		case Mumble::InputEnhancement::Engine::DTLN:
			m_speechCleanupSelection.backend = Settings::DTLNBackend;
			break;
		case Mumble::InputEnhancement::Engine::None:
		case Mumble::InputEnhancement::Engine::Speex:
			break;
	}
	m_speechCleanupSelection.modelId = recipe.modelId();

	if (m_inputEnhancementAutoProfileSwitching) {
		using namespace Mumble::InputEnhancement;
		using Bank = AutoV1::PreparedPipelineBank;

		auto factoryForAutoProfile = [this, packageVerifier, request](Profile target) -> Bank::PipelineFactory {
			ResolveRequest candidateRequest = request;
			candidateRequest.profile        = Profile::Auto;
			switch (target) {
				case Profile::Balanced:
					candidateRequest.cpuClass = CpuClass::Standard;
					break;
				case Profile::Quality:
					candidateRequest.cpuClass = CpuClass::High;
					break;
				case Profile::Original:
				case Profile::Light:
				case Profile::Auto:
				case Profile::VoiceFocus:
					return {};
			}

			const Recipe candidate = RecipeCatalog::resolve(candidateRequest);
			if (candidate.effectiveProfile() != target || !candidate.usesNeuralProcessor()
				|| !packageVerifier->recipeAuthorized(candidate)
				|| !packageVerifier->modelAuthorized(candidate.modelId())) {
				return {};
			}
			const QString modelSha256 = packageVerifier->modelSha256Hex(candidate.modelId());
			const QString modelPath   = packageVerifier->modelPath(candidate.modelId());
			return [this, candidate, modelSha256, modelPath]() -> std::unique_ptr< Pipeline > {
				auto candidatePipeline =
					std::make_unique< Pipeline >(Pipeline::ProcessorFactory{}, Pipeline::NanosecondClock{},
												 Pipeline::defaultFrameDeadlineNanoseconds);
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
				m_speechCleanupE2EModelInitializationAttempts.fetch_add(1, std::memory_order_relaxed);
#endif
				if (!candidatePipeline->configure(candidate, modelSha256, modelPath)) {
					return {};
				}
				return candidatePipeline;
			};
		};

		Bank::PipelineFactory balancedFactory;
		Bank::PipelineFactory crispFactory;
		if (request.cpuClass != CpuClass::Low && request.backendAvailability.rnnoise) {
			balancedFactory = factoryForAutoProfile(Profile::Balanced);
		}
		if (request.cpuClass == CpuClass::High && request.backendAvailability.deepFilterNet) {
			crispFactory = factoryForAutoProfile(Profile::Quality);
		}

		if (recipe.effectiveProfile() == Profile::Light && !balancedFactory && !crispFactory) {
			m_inputEnhancementEngine          = Engine::Speex;
			m_inputEnhancementConcreteProfile = Profile::Light;
			return;
		}

		auto bank                = std::make_unique< Bank >();
		bool usedStartupFallback = false;
		bool initialized         = bank->initialize(recipe.effectiveProfile(), balancedFactory, crispFactory);
		if (!initialized && balancedFactory) {
			qWarning("AudioInput: Auto could not initialize its requested recipe; retrying with Balanced");
			usedStartupFallback = true;
			initialized         = bank->initialize(Profile::Balanced, balancedFactory, crispFactory);
		}
		if (!initialized) {
			qWarning("AudioInput: Auto neural candidates are unavailable; using Light for this session");
			m_inputEnhancementEngine          = Engine::Speex;
			m_inputEnhancementConcreteProfile = Profile::Light;
			m_inputEnhancementAutoPolicy.reset(Profile::Light);
			m_inputEnhancementAutoSwitchGate.reset(Profile::Light);
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			failInputEnhancementProbation(ProbationHealthSignal::InitializationFailure);
			return;
		}

		m_inputEnhancementAutoPipelineBank = std::move(bank);
		m_inputEnhancementConcreteProfile  = m_inputEnhancementAutoPipelineBank->activeProfile();
		m_inputEnhancementEngine = m_inputEnhancementConcreteProfile == Profile::Quality    ? Engine::DeepFilterNet
								   : m_inputEnhancementConcreteProfile == Profile::Balanced ? Engine::RNNoise
																							: Engine::Speex;
		m_inputEnhancementAutoPolicy.reset(m_inputEnhancementConcreteProfile);
		m_inputEnhancementAutoSwitchGate.reset(m_inputEnhancementConcreteProfile);
		if (usedStartupFallback) {
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			failInputEnhancementProbation(ProbationHealthSignal::InitializationFailure);
		}
		return;
	}

	if (!recipe.usesNeuralProcessor()) {
		// The product Light stage owns a dedicated denoise-only Speex state. It is
		// fully prepared here on the control thread and runs before Mumble's
		// established AGC/VAD/dereverb preprocessor in the callback.
		auto pipeline = std::make_unique< Mumble::InputEnhancement::Pipeline >();
		if (!m_inputEnhancementLightProcessor.configure(recipe, *pipeline)) {
			qWarning("AudioInput: Light input enhancement recipe %s failed to initialize; using Original",
					 qUtf8Printable(recipe.id()));
			m_inputEnhancementEngine          = Mumble::InputEnhancement::Engine::None;
			m_inputEnhancementConcreteProfile = Mumble::InputEnhancement::Profile::Original;
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
			m_inputEnhancementLightProcessor.reset();
			return;
		}
		if (!m_inputEnhancementAutoAdapt && activeExactRecipeBinding) {
			m_inputEnhancementCalibrationBaselinePreference = preference;
			m_inputEnhancementCalibrationBaselineBinding    = activeExactRecipeBinding;
			m_inputEnhancementCalibrationBaselineAvailable  = true;
		}
		m_inputEnhancementPipeline = std::move(pipeline);
		return;
	}

	// The 5 ms Balanced and 8 ms Quality/Voice Focus values are p99 qualification goals,
	// not fail-closed thresholds. A single scheduler preemption must not drop an
	// otherwise healthy utterance to Original; the product catastrophe/soak
	// contract uses 10 ms as the hard per-frame deadline.
	const std::uint64_t deadline = 10'000'000;
	auto pipeline                = std::make_unique< Mumble::InputEnhancement::Pipeline >(
		Mumble::InputEnhancement::Pipeline::ProcessorFactory {},
		Mumble::InputEnhancement::Pipeline::NanosecondClock {}, deadline);
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
	++m_speechCleanupE2EModelInitializationAttempts;
#endif
	const bool configured = pipeline->configure(recipe, verifiedModelSha256, verifiedModelPath);
	if (!configured) {
		const Mumble::InputEnhancement::Diagnostics diagnostics = pipeline->diagnostics();
		qWarning("AudioInput: Input enhancement recipe %s failed to initialize (reason=%d); using Original",
				 qUtf8Printable(recipe.id()), static_cast< int >(diagnostics.fallbackReason()));
		m_inputEnhancementEngine          = Mumble::InputEnhancement::Engine::None;
		m_inputEnhancementConcreteProfile = Mumble::InputEnhancement::Profile::Original;
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
	} else if (!m_inputEnhancementAutoAdapt && activeExactRecipeBinding) {
		m_inputEnhancementCalibrationBaselinePreference = preference;
		m_inputEnhancementCalibrationBaselineBinding    = activeExactRecipeBinding;
		m_inputEnhancementCalibrationBaselineAvailable  = true;
	}
	m_inputEnhancementPipeline = std::move(pipeline);
}

void AudioInput::initializeInputEnhancementProbation(
	const Mumble::InputEnhancement::DeviceProfileState *deviceProfile) {
	if (!deviceProfile) {
		return;
	}
	if (!deviceProfile->pendingValidation) {
		m_inputEnhancementProbation.restoreUndo(*deviceProfile);
		return;
	}
	if (!deviceProfile->lastKnownGood
		|| !Mumble::InputEnhancement::executionBindingMatchesPreference(deviceProfile->preference,
																		deviceProfile->pendingRecipeBinding,
																		deviceProfile->pendingAutoRecipeSetFingerprint)
		|| !Mumble::InputEnhancement::executionBindingMatchesPreference(
			*deviceProfile->lastKnownGood, deviceProfile->lastKnownGoodRecipeBinding,
			deviceProfile->lastKnownGoodAutoRecipeSetFingerprint)) {
		return;
	}
	if (deviceProfile->preference.profile == Mumble::InputEnhancement::Profile::Auto) {
		m_inputEnhancementProbation.startAuto(
			deviceProfile->identity, deviceProfile->preference, *deviceProfile->lastKnownGood,
			*deviceProfile->pendingAutoRecipeSetFingerprint, deviceProfile->lastKnownGoodRecipeBinding,
			deviceProfile->lastKnownGoodAutoRecipeSetFingerprint);
	} else {
		m_inputEnhancementProbation.start(deviceProfile->identity, deviceProfile->preference,
										  *deviceProfile->lastKnownGood, *deviceProfile->pendingRecipeBinding,
										  deviceProfile->lastKnownGoodRecipeBinding);
	}
}

void AudioInput::failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal signal) noexcept {
	if (m_inputEnhancementProbation.observeFrame(0, false, signal)
		== Mumble::InputEnhancement::AutoV1::ProbationAction::Rollback) {
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
	}
}

Mumble::InputEnhancement::DeviceIdentity AudioInput::inputDeviceIdentity() const {
	QMutexLocker lock(&m_inputDeviceIdentityMutex);
	return m_inputDeviceIdentity;
}

void AudioInput::confirmOpenedInputDeviceIdentity(Mumble::InputEnhancement::DeviceIdentity identity) noexcept {
	const Mumble::InputEnhancement::DeviceIdentity expected = inputDeviceIdentity();
	const bool matches = Mumble::InputEnhancement::deviceIdentitiesMatch(expected, identity);
	{
		QMutexLocker lock(&m_inputDeviceIdentityMutex);
		m_inputDeviceIdentity = std::move(identity);
	}

	if (matches) {
		return;
	}

	qWarning("AudioInput: Opened microphone differs from pre-warmed identity; using Original for this input session");
	m_usesLegacyInputEnhancement           = false;
	m_inputEnhancementAutoAdapt            = false;
	m_inputEnhancementAutoProfileSwitching = false;
	m_inputEnhancementEngine               = Mumble::InputEnhancement::Engine::None;
	m_inputEnhancementConcreteProfile      = Mumble::InputEnhancement::Profile::Original;
	m_inputEnhancementLightProcessor.reset();
	m_inputEnhancementPipeline.reset();
	m_inputEnhancementAutoPipelineBank.reset();
	m_speechCleanupProcessor.reset();
	m_configuredLegacyNoiseCancelMode = Settings::NoiseCancelOff;
	m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
	failInputEnhancementProbation(Mumble::InputEnhancement::ProbationHealthSignal::InitializationFailure);
}

void AudioInput::markOpenedInputDeviceProfileUsed() noexcept {
	// Backends call this only after the complete capture chain has opened and
	// started. A prepared model/binding alone is not sufficient rollback proof.
	m_inputEnhancementCaptureOpened.store(true, std::memory_order_release);
	const Mumble::InputEnhancement::DeviceIdentity openedIdentity = inputDeviceIdentity();
	if (!openedIdentity.stable) {
		return;
	}
	QMetaObject::invokeMethod(
		this,
		[openedIdentity]() {
			if (Mumble::InputEnhancement::markDeviceProfileUsed(Global::get().s.inputEnhancement, openedIdentity,
																QDateTime::currentMSecsSinceEpoch())) {
				Global::get().s.save();
			}
		},
		Qt::QueuedConnection);
}

Mumble::InputEnhancement::Pipeline *AudioInput::activeInputEnhancementPipeline() noexcept {
	return m_inputEnhancementAutoPipelineBank ? m_inputEnhancementAutoPipelineBank->activePipeline()
											  : m_inputEnhancementPipeline.get();
}

const Mumble::InputEnhancement::Pipeline *AudioInput::activeInputEnhancementPipeline() const noexcept {
	return m_inputEnhancementAutoPipelineBank ? m_inputEnhancementAutoPipelineBank->activePipeline()
											  : m_inputEnhancementPipeline.get();
}

bool AudioInput::neuralInputEnhancementReady() const noexcept {
	if (m_usesLegacyInputEnhancement) {
		return (noiseCancel == Settings::NoiseCancelRNN || noiseCancel == Settings::NoiseCancelBoth)
			   && m_speechCleanupProcessor && m_speechCleanupProcessor->isReady();
	}

	const bool neuralEngine = m_inputEnhancementEngine == Mumble::InputEnhancement::Engine::RNNoise
							  || m_inputEnhancementEngine == Mumble::InputEnhancement::Engine::DeepFilterNet
							  || m_inputEnhancementEngine == Mumble::InputEnhancement::Engine::DTLN;
	const Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	return neuralEngine && pipeline && (!pipeline->fallbackActive() || pipeline->alignedFallbackActive());
}

bool AudioInput::alignedInputEnhancementFallbackActive() const noexcept {
	const Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	return !m_usesLegacyInputEnhancement && pipeline && pipeline->alignedFallbackActive();
}

void AudioInput::finishAlignedInputEnhancementFallback() noexcept {
	using Engine                           = Mumble::InputEnhancement::Engine;
	using Profile                          = Mumble::InputEnhancement::Profile;
	m_inputEnhancementAutoAdapt            = false;
	m_inputEnhancementAutoProfileSwitching = false;

	const Profile recoveryProfile = m_inputEnhancementLastWorkingProfile;
	if (m_inputEnhancementRuntimeRecoveryPending && m_inputEnhancementAutoPipelineBank
		&& recoveryProfile != Profile::Original
		&& m_inputEnhancementAutoPipelineBank->candidatePrepared(recoveryProfile)
		&& m_inputEnhancementAutoPipelineBank->switchTo(recoveryProfile)) {
		m_inputEnhancementEngine          = recoveryProfile == Profile::Quality    ? Engine::DeepFilterNet
											: recoveryProfile == Profile::Balanced ? Engine::RNNoise
																				   : Engine::Speex;
		m_inputEnhancementConcreteProfile = recoveryProfile;
		noiseCancel = m_inputEnhancementEngine == Engine::Speex ? Settings::NoiseCancelSpeex : Settings::NoiseCancelRNN;
		// Auto cannot currently recover to Light, but if that path is enabled later
		// product Speex still belongs to the dedicated LightProcessor rather than
		// the common OG preprocessor.
		m_preprocessor.setDenoise(false);
		m_inputEnhancementAutoPolicy.reset(recoveryProfile);
		m_inputEnhancementAutoSwitchGate.reset(recoveryProfile);
		m_inputEnhancementRuntimeRecoveryPending = false;
		return;
	}

	m_inputEnhancementRuntimeRecoveryPending = false;
	m_inputEnhancementEngine                 = Engine::None;
	m_inputEnhancementConcreteProfile        = Profile::Original;
	m_inputEnhancementAutoAdapt              = false;
	m_inputEnhancementAutoProfileSwitching   = false;
	noiseCancel                              = Settings::NoiseCancelOff;
	m_preprocessor.setDenoise(false);
}

unsigned int AudioInput::inputEnhancementLatencySamples() const noexcept {
	if (m_usesLegacyInputEnhancement) {
		return neuralInputEnhancementReady() ? m_speechCleanupProcessor->latencySamples() : 0;
	}
	const Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	return pipeline ? pipeline->latencySamples() : 0;
}

bool AudioInput::processInputEnhancementFrame(std::array< float, Mumble::InputEnhancement::frameSamples > &frame,
											  unsigned int validSamples) noexcept {
	if (!neuralInputEnhancementReady()) {
		return false;
	}
	if (m_usesLegacyInputEnhancement) {
		m_speechCleanupProcessor->processInPlace(frame.data(), validSamples);
		return true;
	}
	if (validSamples != Mumble::InputEnhancement::frameSamples) {
		return false;
	}
	Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	if (!pipeline) {
		return false;
	}
	if (m_inputEnhancementAutoAdapt) {
		return pipeline->processFrame(frame.data(), validSamples,
			Mumble::InputEnhancement::mixFactorForControls(m_inputEnhancementConcreteProfile,
				m_inputEnhancementActiveReduction, m_inputEnhancementActiveCharacter));
	}
	return pipeline->processFrame(frame);
}

Mumble::InputEnhancement::AutoV1::DeadlinePressure AudioInput::inputEnhancementDeadlinePressure() const noexcept {
	using Pressure                                     = Mumble::InputEnhancement::AutoV1::DeadlinePressure;
	const Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	if (!pipeline || !neuralInputEnhancementReady() || m_usesLegacyInputEnhancement) {
		return Pressure::None;
	}
	const std::uint64_t elapsed =
		std::max(pipeline->lastProcessingNanoseconds(), pipeline->lastWorkerProcessingNanoseconds());
	const std::uint64_t deadline = pipeline->frameDeadlineNanoseconds();
	if (deadline == 0 || elapsed > deadline) {
		return Pressure::Critical;
	}
	Pressure pressure = Pressure::None;
	if (elapsed >= deadline - deadline / 5) {
		pressure = Pressure::High;
	} else if (elapsed >= (deadline + 1) / 2) {
		pressure = Pressure::Elevated;
	}

	const unsigned int pendingFrames    = pipeline->workerPendingFrames();
	const unsigned int schedulingFrames = pipeline->workerSchedulingDelayFrames();
	if (schedulingFrames > 0 && pendingFrames > schedulingFrames) {
		return Pressure::Critical;
	}
	if (schedulingFrames > 0 && pendingFrames == schedulingFrames) {
		return std::max(pressure, Pressure::High);
	}
	if (pendingFrames > 0 && pipeline->workerSchedulingSlackFrames() <= 1) {
		return std::max(pressure, Pressure::Elevated);
	}
	return pressure;
}

bool AudioInput::autoInputEnhancementCandidatePrepared(Mumble::InputEnhancement::Profile profile) const noexcept {
	using Profile = Mumble::InputEnhancement::Profile;
	if (profile == m_inputEnhancementConcreteProfile) {
		return true;
	}
	return m_inputEnhancementAutoPipelineBank && m_inputEnhancementAutoPipelineBank->candidatePrepared(profile);
}

bool AudioInput::applyPreparedAutoInputEnhancementProfile(Mumble::InputEnhancement::Profile profile) noexcept {
	using Engine  = Mumble::InputEnhancement::Engine;
	using Profile = Mumble::InputEnhancement::Profile;
	if (profile == m_inputEnhancementConcreteProfile) {
		return true;
	}

	if (!m_inputEnhancementAutoPipelineBank || !m_inputEnhancementAutoPipelineBank->switchTo(profile)) {
		return false;
	}

	const Profile previousProfile     = m_inputEnhancementConcreteProfile;
	const Engine nextEngine           = profile == Profile::Quality    ? Engine::DeepFilterNet
										: profile == Profile::Balanced ? Engine::RNNoise
																	   : Engine::Speex;
	m_inputEnhancementEngine          = nextEngine;
	m_inputEnhancementConcreteProfile = profile;
	m_inputEnhancementAutoPolicy.commitProfile(profile);
	if (previousProfile != Profile::Original) {
		m_inputEnhancementLastWorkingProfile = previousProfile;
	}
	noiseCancel = nextEngine == Engine::Speex ? Settings::NoiseCancelSpeex : Settings::NoiseCancelRNN;
	m_preprocessor.setDenoise(nextEngine == Engine::Speex);
	return true;
}

void AudioInput::updateAutoInputEnhancement(float vadConfidence, bool acousticSpeech) noexcept {
	using namespace Mumble::InputEnhancement;
	using namespace Mumble::InputEnhancement::AutoV1;
	if (!m_inputEnhancementAutoAdapt || m_usesLegacyInputEnhancement
		|| m_inputEnhancementConcreteProfile == Profile::Original) {
		return;
	}

	Observation observation;
	if (m_inputEnhancementAutoTracker.produceObservation(
			vadConfidence, m_inputEnhancementCpuClass, inputEnhancementDeadlinePressure(),
			m_inputEnhancementBaseReduction, m_inputEnhancementBaseCharacter, observation)) {
		observation.balancedAvailable = autoInputEnhancementCandidatePrepared(Profile::Balanced);
		observation.crispAvailable    = autoInputEnhancementCandidatePrepared(Profile::Quality);
		observation.allowProfileSwitch =
			m_inputEnhancementAutoProfileSwitching && m_inputEnhancementAutoSwitchGate.switchingAllowed();
		const Decision decision           = m_inputEnhancementAutoPolicy.evaluate(observation);
		m_inputEnhancementActiveReduction = decision.noiseReduction;
		m_inputEnhancementActiveCharacter = decision.naturalCrisp;
		m_inputEnhancementSpeexStrength =
			Mumble::InputEnhancement::lightSpeexSuppressionDb(decision.noiseReduction);
		m_inputEnhancementAutoSwitchGate.reserve(decision);
	}

	const bool idleBoundary =
		m_inputEnhancementAutoSilenceBoundary.observe(acousticSpeech, m_speechCleanupTransmitDrain.active());
	if (!m_inputEnhancementAutoSwitchGate.pending()) {
		return;
	}
	const Profile pending             = m_inputEnhancementAutoSwitchGate.pendingProfile();
	const bool candidatePrepared      = autoInputEnhancementCandidatePrepared(pending);
	const SwitchGateResult gateResult = m_inputEnhancementAutoSwitchGate.poll(idleBoundary, candidatePrepared);
	if (gateResult.action == SwitchGateAction::ApplyPrepared) {
		if (applyPreparedAutoInputEnhancementProfile(gateResult.profile)) {
			return;
		}
	} else if (gateResult.action == SwitchGateAction::RejectUnavailable) {
		// Fall through to the transactional rollback below.
	} else {
		return;
	}

	const Profile activeProfile       = m_inputEnhancementAutoPipelineBank
											? m_inputEnhancementAutoPipelineBank->activeProfile()
											: m_inputEnhancementConcreteProfile;
	m_inputEnhancementConcreteProfile = activeProfile;
	m_inputEnhancementAutoPolicy.reset(activeProfile);
	m_inputEnhancementAutoSwitchGate.reset(activeProfile);
	m_inputEnhancementAutoProfileSwitching = false;
	m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
	failInputEnhancementProbation(ProbationHealthSignal::InitializationFailure);
}

void AudioInput::selectNoiseCancel() {
	Settings::SpeechCleanupBackend backend = m_speechCleanupSelection.backend;
	if (m_usesLegacyInputEnhancement) {
		noiseCancel = m_configuredLegacyNoiseCancelMode;
	} else {
		switch (m_inputEnhancementEngine) {
			case Mumble::InputEnhancement::Engine::None:
				noiseCancel = Settings::NoiseCancelOff;
				break;
			case Mumble::InputEnhancement::Engine::Speex:
				noiseCancel = Settings::NoiseCancelSpeex;
				break;
			case Mumble::InputEnhancement::Engine::RNNoise:
				backend     = Settings::RNNoiseBackend;
				noiseCancel = Settings::NoiseCancelRNN;
				break;
			case Mumble::InputEnhancement::Engine::DeepFilterNet:
				backend     = Settings::DeepFilterNetBackend;
				noiseCancel = Settings::NoiseCancelRNN;
				break;
			case Mumble::InputEnhancement::Engine::DTLN:
				backend     = Settings::DTLNBackend;
				noiseCancel = Settings::NoiseCancelRNN;
				break;
		}
	}

	if ((noiseCancel == Settings::NoiseCancelRNN || noiseCancel == Settings::NoiseCancelBoth)
		&& !neuralInputEnhancementReady()) {
		qWarning("AudioInput: Neural input enhancement is unavailable; using Original");
		noiseCancel = Settings::NoiseCancelOff;
	}

	bool preprocessorDenoise = false;
	switch (noiseCancel) {
		case Settings::NoiseCancelOff:
			qInfo("AudioInput: Input enhancement uses Original");
			break;
		case Settings::NoiseCancelSpeex:
			qInfo("AudioInput: Input enhancement uses Light (Speex)");
			// Product Light has a separately prepared denoise-only Speex state
			// before this common processor. Legacy Speex keeps its original path.
			preprocessorDenoise = m_usesLegacyInputEnhancement;
			break;
		case Settings::NoiseCancelRNN:
			qInfo("AudioInput: Input enhancement uses %s", Mumble::SpeechCleanup::backendDisplayName(backend));
			break;
		case Settings::NoiseCancelBoth:
			preprocessorDenoise = true;
			qInfo("AudioInput: Legacy input enhancement uses %s and Speex",
				  Mumble::SpeechCleanup::backendDisplayName(backend));
			break;
	}
	m_preprocessor.setDenoise(preprocessorDenoise);
}

const SpeechCleanupProcessor *AudioInput::speechCleanupProcessorForDiagnostics() const noexcept {
	return m_speechCleanupProcessor.get();
}

const Mumble::SpeechCleanup::Selection &AudioInput::speechCleanupSelectionForDiagnostics() const noexcept {
	return m_speechCleanupSelection;
}

bool AudioInput::speechCleanupReadyForDiagnostics() const noexcept {
	return neuralInputEnhancementReady();
}

QString AudioInput::speechCleanupActiveModelIdForDiagnostics() const {
	if (m_usesLegacyInputEnhancement) {
		return m_speechCleanupProcessor ? m_speechCleanupProcessor->activeModelId() : QString();
	}
	const Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	return pipeline ? pipeline->diagnostics().activeModelId() : QString();
}

QString AudioInput::speechCleanupActiveModelPathForDiagnostics() const {
	return m_usesLegacyInputEnhancement && m_speechCleanupProcessor ? m_speechCleanupProcessor->activeModelPath()
																	: QString();
}

bool AudioInput::speechCleanupUsedFallbackForDiagnostics() const {
	if (m_usesLegacyInputEnhancement) {
		return m_speechCleanupProcessor && m_speechCleanupProcessor->usedFallback();
	}
	const Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline();
	return pipeline && pipeline->fallbackActive();
}

unsigned int AudioInput::speechCleanupLatencyForDiagnostics() const noexcept {
	return inputEnhancementLatencySamples();
}

const Mumble::InputEnhancement::Pipeline *AudioInput::inputEnhancementPipelineForDiagnostics() const noexcept {
	return m_usesLegacyInputEnhancement ? nullptr : activeInputEnhancementPipeline();
}

#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
bool AudioInput::usesLegacyInputEnhancementForDiagnostics() const noexcept {
	return m_usesLegacyInputEnhancement;
}

Mumble::InputEnhancement::Profile AudioInput::inputEnhancementProfileForDiagnostics() const noexcept {
	const Mumble::InputEnhancement::Pipeline *pipeline = inputEnhancementPipelineForDiagnostics();
	return pipeline ? pipeline->diagnostics().activeProfile() : m_inputEnhancementConcreteProfile;
}

std::uint64_t AudioInput::inputEnhancementModelInitializationAttemptsForDiagnostics() const noexcept {
	return m_speechCleanupE2EModelInitializationAttempts.load(std::memory_order_relaxed);
}

unsigned int AudioInput::finishSpeechCleanupE2ETransmission() {
	// This is an explicit release rather than a VAD decision, so begin the drain
	// before submitting the first zero frame. That makes the deterministic path
	// consume exactly latencySamples() zeros instead of one decision frame plus
	// the reported latency. Run a fresh drain even when VAD already closed the
	// utterance: noisy raw-VAD restarts may have cancelled every earlier drain,
	// and release qualification needs one complete, unambiguous final proof.
	unsigned int requestedDrainSamples = 0;
	qInfo("AudioInput E2E drain: begin previousVoice=%s neuralReady=%s activeBefore=%s remainingBefore=%u",
		  bPreviousVoice ? "true" : "false", neuralInputEnhancementReady() ? "true" : "false",
		  m_speechCleanupTransmitDrain.active() ? "true" : "false",
		  m_speechCleanupTransmitDrain.remainingSamples());
	requestedDrainSamples = inputEnhancementLatencySamples();
	m_speechCleanupE2ECurrentDrainSamples       = 0;
	m_speechCleanupE2ELastCompletedDrainSamples = 0;
	// The unchanged common OG Speex stage emits the preceding callback even when
	// denoise is disabled. One callback-only flush therefore has to follow the
	// enhancement's own causal drain. It is not enhancement latency and is not
	// included in the returned drained-sample count. For Original this also emits
	// a real terminator instead of making the deterministic receiver time out.
	constexpr unsigned int commonPreprocessorFlushFrames = 1;
	m_speechCleanupTransmitDrain.begin(requestedDrainSamples, commonPreprocessorFlushFrames);
	std::vector< short > silence(iFrameSize, 0);
	m_forceSpeechCleanupE2ERelease = true;
	auto nextDeadline              = std::chrono::steady_clock::now();
	const unsigned int maximumDrainCallbacks =
		((requestedDrainSamples + iFrameSize - 1) / iFrameSize) + commonPreprocessorFlushFrames;
	for (unsigned int callback = 0;
		 callback < maximumDrainCallbacks && m_speechCleanupTransmitDrain.active(); ++callback) {
		encodeAudioFrame(AudioChunk(silence.data()));
		qInfo("AudioInput E2E drain: callback active=%s remaining=%u current=%u completed=%u",
			  m_speechCleanupTransmitDrain.active() ? "true" : "false",
			  m_speechCleanupTransmitDrain.remainingSamples(), m_speechCleanupE2ECurrentDrainSamples,
			  m_speechCleanupE2ELastCompletedDrainSamples);
		// This helper is compiled only into the deterministic E2E client. A real
		// capture backend receives each drain frame from its next 10 ms callback;
		// preserve that contract here so an asynchronous neural worker is tested
		// against its actual deadline instead of an artificial zero-time burst.
		nextDeadline += std::chrono::milliseconds(10);
		std::this_thread::sleep_until(nextDeadline);
	}
	// The last causal drain callback may leave one asynchronous worker job in
	// flight. Its result is beyond the emitted timeline, so submitting another
	// callback would incorrectly extend the capture. Instead, wait here (this
	// deterministic helper runs outside the real-time callback) before final
	// diagnostics are snapshotted. This observes all submitted worker failures
	// without emitting audio or weakening the fixed-window edge/tail gates.
	bool offlineProcessingFinished = true;
	if (m_usesLegacyInputEnhancement) {
		if (m_speechCleanupProcessor) {
			offlineProcessingFinished = m_speechCleanupProcessor->finishOfflineProcessing();
		}
	} else if (Mumble::InputEnhancement::Pipeline *pipeline = activeInputEnhancementPipeline()) {
		offlineProcessingFinished = pipeline->finishOfflineProcessing();
	}
	qInfo("AudioInput E2E drain: offline worker finish=%s",
		  offlineProcessingFinished ? "true" : "false");
	m_forceSpeechCleanupE2ERelease = false;
	return m_speechCleanupE2ELastCompletedDrainSamples;
}
#endif

int AudioInput::encodeOpusFrame(short *source, int size, EncodingOutputBuffer &buffer) {
	int len;
	if (bResetEncoder) {
		opus_encoder_ctl(opusState, OPUS_RESET_STATE, nullptr);
		bResetEncoder = false;
	}

	opus_encoder_ctl(opusState, OPUS_SET_BITRATE(iAudioQuality));

	len = opus_encode(opusState, source, size, &buffer[0], static_cast< opus_int32 >(buffer.size()));
	const int tenMsFrameCount = (size / iFrameSize);
	iBitrate                  = (len * 100 * 8) / tenMsFrameCount;
	m_voiceActivityBitrate.store(std::max(0, iBitrate), std::memory_order_relaxed);
	return len;
}

void AudioInput::encodeAudioFrame(AudioChunk chunk) {
	float sum;
	short max;

	short *psSource;

	iFrameCounter++;

	// As Global::get().iTarget is not protected by any locks, we avoid race-conditions by
	// copying it once at this point and stick to whatever value it is here. Thus
	// if the value of Global::get().iTarget changes during the execution of this function,
	// it won't cause any inconsistencies and the change is reflected once this
	// function is called again.
	std::int32_t voiceTargetID = Global::get().iTarget;

	if (!bRunning)
		return;

	VoiceActivationDebugCapture &diagnosticCapture = VoiceActivationDebugCapture::instance();
	const bool captureVoiceDiagnostic              = diagnosticCapture.enabled();
	const std::uint64_t diagnosticTimestampUs =
		captureVoiceDiagnostic ? diagnosticCapture.timestampMicroseconds() : 0;
	std::array< short, iFrameSize > diagnosticRawInput = {};
	if (captureVoiceDiagnostic) {
		std::copy_n(chunk.mic, iFrameSize, diagnosticRawInput.begin());
	}

	sum = 1.0f;
	max = 1;
	for (unsigned int i = 0; i < iFrameSize; i++) {
		sum += static_cast< float >(chunk.mic[i] * chunk.mic[i]);
		max = std::max(static_cast< short >(abs(chunk.mic[i])), max);
	}
	dPeakMic = qMax(20.0f * log10f(sqrtf(sum / static_cast< float >(iFrameSize)) / 32768.0f), -96.0f);
	dMaxMic  = max;

	if (chunk.speaker && (iEchoChannels > 0)) {
		sum = 1.0f;
		for (unsigned int i = 0; i < iEchoFrameSize; ++i) {
			sum += static_cast< float >(chunk.speaker[i] * chunk.speaker[i]);
		}
		dPeakSpeaker = qMax(20.0f * log10f(sqrtf(sum / static_cast< float >(iFrameSize)) / 32768.0f), -96.0f);
	} else {
		dPeakSpeaker = 0.0;
	}

	QMutexLocker l(&qmSpeex);
	resetAudioProcessor();

	const std::int32_t gainValue = m_preprocessor.getAGCGain();

	if (m_usesLegacyInputEnhancement
		&& (noiseCancel == Settings::NoiseCancelSpeex || noiseCancel == Settings::NoiseCancelBoth)) {
		m_preprocessor.setNoiseSuppress(m_inputEnhancementSpeexStrength - gainValue);
	}

	const bool neuralCleanupReady = neuralInputEnhancementReady();
	Mumble::InputEnhancement::Pipeline *classicProductPipeline =
		!m_usesLegacyInputEnhancement && m_inputEnhancementEngine == Mumble::InputEnhancement::Engine::Speex
			&& m_inputEnhancementLightProcessor.ready() ? activeInputEnhancementPipeline()
			: nullptr;
	const unsigned int cleanupLatencySamples = inputEnhancementLatencySamples();
	const bool cleanupTailReady = cleanupLatencySamples > 0
		&& (neuralCleanupReady || classicProductPipeline != nullptr);
	Mumble::InputEnhancement::ProbationHealthSignal probationHealth =
		Mumble::InputEnhancement::ProbationHealthSignal::Healthy;

	// If Global::get().iPushToTalk > 0 that means that we are currently in some sort of PTT action. For
	// instance this could mean we're currently whispering.
	bool isPTT = Global::get().iPushToTalk > 0;
	if (Global::get().s.atTransmit == Settings::PushToTalk) {
		const bool doublePush = Global::get().s.uiDoublePush > 0
								&& ((Global::get().uiDoublePush < Global::get().s.uiDoublePush)
									|| (static_cast< quint64 >(Global::get().tDoublePush.elapsed().count())
										< Global::get().s.uiDoublePush));
		// With double push enabled, we might be in a PTT state without pressing any PTT key.
		isPTT = isPTT || doublePush;
	}

	const bool continuousTransmission = Global::get().s.atTransmit == Settings::Continuous
										|| API::PluginData::get().overwriteMicrophoneActivation.load();
	bool forceSpeechCleanupE2ERelease = false;
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
	forceSpeechCleanupE2ERelease = m_forceSpeechCleanupE2ERelease;
#endif

	// A fresh activation must take over the existing streaming state without a
	// processor reset. Raw amplitude is used while draining because the cleanup
	// input itself has to remain exact zero until its causal tail has been sent.
	const float rawAmplitudeLevel = std::clamp(1.0f + dPeakMic / 96.0f, 0.0f, 1.0f);
	const bool rawVADRestart =
		Global::get().s.atTransmit == Settings::VAD
		&& voiceActivityTriggers(rawAmplitudeLevel, Global::get().s.fVADmin, Global::get().s.fVADmax, false);
	const bool speechCleanupDrainActivation =
		isPTT || rawVADRestart || (continuousTransmission && !forceSpeechCleanupE2ERelease);
	bool speechCleanupDrainCancelledForActivation = false;
	const bool explicitE2ECommonFlush = forceSpeechCleanupE2ERelease
		&& m_speechCleanupTransmitDrain.remainingSamples() == 0
		&& m_speechCleanupTransmitDrain.remainingTerminalFlushFrames() > 0;
	if (m_speechCleanupTransmitDrain.active() && !cleanupTailReady && !explicitE2ECommonFlush) {
		m_speechCleanupTransmitDrain.cancel();
	} else if (m_speechCleanupTransmitDrain.active() && speechCleanupDrainActivation
			   && !forceSpeechCleanupE2ERelease) {
		m_speechCleanupTransmitDrain.cancel();
		speechCleanupDrainCancelledForActivation = true;
	}

	Mumble::SpeechCleanup::TransmitDrain::Frame speechCleanupDrainFrame;
	if (m_speechCleanupTransmitDrain.active()) {
		speechCleanupDrainFrame = m_speechCleanupTransmitDrain.takeFrame(iFrameSize);
	}

	short psClean[iFrameSize];
	if (chunk.speaker && Global::get().s.echoOption == EchoCancelOptionID::WEBRTC_AEC && m_webrtcEchoCanceller
		&& m_webrtcEchoCanceller->isReady()
		&& m_webrtcEchoCanceller->processCaptureFrame(chunk.mic, psClean, chunk.speaker, iFrameSize,
													  bEchoMulti ? iEchoChannels : 1)) {
		psSource = psClean;
	} else if (sesEcho && chunk.speaker) {
		speex_echo_cancellation(sesEcho, chunk.mic, chunk.speaker, psClean);
		psSource = psClean;
	} else {
		psSource = chunk.mic;
	}
	Mumble::InputEnhancement::CalibrationRuntimeBridge *calibrationRuntime =
		m_inputEnhancementCalibrationForCallback.load(std::memory_order_acquire);
	if (calibrationRuntime) {
		calibrationRuntime->appendPcmFromCallback(psSource, iFrameSize);
	}
	if (m_inputEnhancementAutoAdapt && !m_usesLegacyInputEnhancement) {
		m_inputEnhancementAutoTracker.captureFrame(psSource, iFrameSize);
	}
	if (classicProductPipeline && speechCleanupDrainFrame.draining) {
		// Speex owns a real one-frame causal delay. Feed exact zero PCM while
		// draining it, just like the neural path, rather than letting room noise
		// replace the final 10 ms of the preceding utterance.
		std::fill(psClean, psClean + iFrameSize, 0);
		psSource = psClean;
	}
	if (neuralCleanupReady) {
		std::array< float, 480 > cleanupFrame = {};
		unsigned int cleanupSampleCount       = static_cast< unsigned int >(cleanupFrame.size());
		if (speechCleanupDrainFrame.draining) {
			cleanupSampleCount = speechCleanupDrainFrame.zeroInputSamples;
		} else {
			for (unsigned int i = 0; i < cleanupFrame.size(); ++i) {
				cleanupFrame[i] = static_cast< float >(psSource[i]) / 32768.0f;
			}
		}

		const bool processed = processInputEnhancementFrame(cleanupFrame, cleanupSampleCount);
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
		if (processed && speechCleanupDrainFrame.draining) {
			m_speechCleanupE2ECurrentDrainSamples += speechCleanupDrainFrame.causalDrainSamples;
			if (speechCleanupDrainFrame.terminator) {
				m_speechCleanupE2ELastCompletedDrainSamples = m_speechCleanupE2ECurrentDrainSamples;
			}
		}
#endif

		for (unsigned int i = 0; i < cleanupSampleCount; ++i) {
			psSource[i] = clampFloatSample(cleanupFrame[i] * 32768.0f);
		}
		if (speechCleanupDrainFrame.draining) {
			std::fill(psSource + cleanupSampleCount, psSource + iFrameSize, 0);
		}
		Mumble::InputEnhancement::Pipeline *activePipeline = activeInputEnhancementPipeline();
		if (!processed && !m_usesLegacyInputEnhancement && activePipeline && activePipeline->fallbackActive()) {
			switch (activePipeline->fallbackReason()) {
				case Mumble::InputEnhancement::FallbackReason::DeadlineExceeded:
					probationHealth = Mumble::InputEnhancement::ProbationHealthSignal::DeadlineMiss;
					break;
				case Mumble::InputEnhancement::FallbackReason::None:
					break;
				default:
					probationHealth = Mumble::InputEnhancement::ProbationHealthSignal::InvalidOutput;
					break;
			}
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			m_inputEnhancementAutoAdapt            = false;
			m_inputEnhancementAutoProfileSwitching = false;
			m_inputEnhancementRuntimeRecoveryPending =
				m_inputEnhancementAutoPipelineBank
				&& m_inputEnhancementLastWorkingProfile != Mumble::InputEnhancement::Profile::Original
				&& m_inputEnhancementAutoPipelineBank->candidatePrepared(m_inputEnhancementLastWorkingProfile);
			// A runtime failure remains on the already established causal timeline.
			// Only an idle, non-activating path may return to zero-latency Original
			// immediately; an active utterance must recover the delayed dry tail.
			const bool preserveTimeline =
				alignedInputEnhancementFallbackActive()
				&& (bPreviousVoice || speechCleanupDrainActivation || speechCleanupDrainFrame.draining);
			if (!preserveTimeline) {
				finishAlignedInputEnhancementFallback();
				m_speechCleanupTransmitDrain.cancel();
			}
		}
	}

	if (classicProductPipeline) {
		[[maybe_unused]] const bool processed = m_inputEnhancementLightProcessor.processFrame(psSource, iFrameSize);
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
		if ((processed || classicProductPipeline->alignedFallbackActive()) && speechCleanupDrainFrame.draining) {
			m_speechCleanupE2ECurrentDrainSamples += speechCleanupDrainFrame.causalDrainSamples;
			if (speechCleanupDrainFrame.terminator) {
				m_speechCleanupE2ELastCompletedDrainSamples = m_speechCleanupE2ECurrentDrainSamples;
			}
		}
#endif
		if (classicProductPipeline->fallbackActive()) {
			m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
			m_inputEnhancementAutoAdapt            = false;
			m_inputEnhancementAutoProfileSwitching = false;
			const Mumble::InputEnhancement::ProbationHealthSignal lightHealth =
				classicProductPipeline->fallbackReason() == Mumble::InputEnhancement::FallbackReason::DeadlineExceeded
					? Mumble::InputEnhancement::ProbationHealthSignal::DeadlineMiss
					: Mumble::InputEnhancement::ProbationHealthSignal::InvalidOutput;
			failInputEnhancementProbation(lightHealth);
			const bool preserveTimeline = alignedInputEnhancementFallbackActive()
				&& (bPreviousVoice || speechCleanupDrainActivation || speechCleanupDrainFrame.draining);
			if (!preserveTimeline) {
				finishAlignedInputEnhancementFallback();
				m_speechCleanupTransmitDrain.cancel();
			}
		}
	}
	// Keep OG Mumble's common VAD/AGC/dereverb stage after every enhancement
	// profile. Product Light arrives here already mixed and with denoise disabled
	// on this common preprocessor, so both its dry and wet paths receive exactly
	// the same established post-processing.
	m_preprocessor.run(*psSource);

	sum = 1.0f;
	for (unsigned int i = 0; i < iFrameSize; i++)
		sum += static_cast< float >(psSource[i] * psSource[i]);
	float micLevel = sqrtf(sum / static_cast< float >(iFrameSize));
	dPeakSignal    = qMax(20.0f * log10f(micLevel / 32768.0f), -96.0f);
	m_voiceActivityPeakSignalDb.store(dPeakSignal, std::memory_order_relaxed);

	if (bDebugDumpInput) {
		outMic.write(reinterpret_cast< const char * >(chunk.mic), iFrameSize * sizeof(short));
		if (chunk.speaker) {
			outSpeaker.write(reinterpret_cast< const char * >(chunk.speaker),
							 static_cast< std::streamsize >(iEchoFrameSize * sizeof(short)));
		}
		outProcessed.write(reinterpret_cast< const char * >(psSource),
						   static_cast< std::streamsize >(iFrameSize * sizeof(short)));
	}

	fSpeechProb = static_cast< float >(m_preprocessor.getSpeechProb()) / 100.0f;
	m_voiceActivitySpeechProbability.store(fSpeechProb, std::memory_order_relaxed);

	// clean microphone level: peak of filtered signal attenuated by AGC gain
	dPeakCleanMic = qMax(dPeakSignal - static_cast< float >(gainValue), -96.0f);
	m_voiceActivityPeakCleanMicDb.store(dPeakCleanMic, std::memory_order_relaxed);
	const float amplitudeLevel = amplitudeVoiceActivityLevel();
	float level                = voiceActivityLevelFor(Global::get().s.vsVAD, amplitudeLevel, fSpeechProb);

	bool bIsSpeech = false;
	bool vadCandidate = false;
	bool gateAllowed  = false;

	if (!speechCleanupDrainFrame.draining) {
		vadCandidate =
			voiceActivityTriggers(level, Global::get().s.fVADmin, Global::get().s.fVADmax, bPreviousVoice);
		gateAllowed = inputGateAllowsSpeech(vadCandidate, amplitudeLevel, fSpeechProb);
		bIsSpeech   = gateAllowed;
		if (!bIsSpeech && Global::get().s.inputGateMode != Settings::InputGateOff) {
			iHoldFrames = Global::get().s.iVoiceHold;
		}

		if (!bIsSpeech) {
			iHoldFrames++;
			if (iHoldFrames < Global::get().s.iVoiceHold)
				// Hold mic open until iVoiceHold threshold is reached
				bIsSpeech = true;
		} else {
			iHoldFrames = 0;
		}
	}
	const bool acousticSpeech = bIsSpeech;

	if (continuousTransmission) {
		// Continuous transmission is enabled
		bIsSpeech = true;
	} else if (Global::get().s.atTransmit == Settings::PushToTalk) {
		// PTT is enabled, so check if it is currently active.
		bIsSpeech = isPTT;
	}

	bIsSpeech = bIsSpeech || isPTT;
	if (forceSpeechCleanupE2ERelease) {
		bIsSpeech = false;
	}
	const bool probationSpeech = bIsSpeech;

	ClientUser *p                             = ClientUser::get(Global::get().uiSession);
	bool bTalkingWhenMuted                    = false;
	const bool calibrationTransmissionBlocked = m_inputEnhancementCalibrationTransmissionBlock.blocked()
												|| (calibrationRuntime && calibrationRuntime->transmissionBlocked());
	const bool transmissionBlocked =
		Global::get().s.bMute || ((Global::get().s.lmLoopMode != Settings::Local) && p && (p->bMute || p->bSuppress))
		|| Global::get().bPushToMute || (voiceTargetID < 0) || calibrationTransmissionBlocked;
	if (transmissionBlocked) {
		bTalkingWhenMuted = bIsSpeech;
		bIsSpeech         = false;
	}

	bool speechCleanupDrainStarted = false;
	if (transmissionBlocked) {
		m_speechCleanupTransmitDrain.cancel();
		if (alignedInputEnhancementFallbackActive()) {
			// Nothing from this path can be transmitted, so keeping an artificial
			// delay serves no continuity purpose.
			finishAlignedInputEnhancementFallback();
		}
	} else if (speechCleanupDrainCancelledForActivation) {
		// The current frame contains the fresh activation that cancelled the
		// zero-input drain. Keep the existing utterance open while the normal VAD
		// and input-gate state catch up on the real processor output.
		bIsSpeech = true;
	} else if (speechCleanupDrainFrame.draining) {
		// Keep sending until all causal output has been recovered. Only the frame
		// that consumes the final outstanding samples carries the terminator.
		bIsSpeech = !speechCleanupDrainFrame.terminator;
		if (speechCleanupDrainFrame.terminator && alignedInputEnhancementFallbackActive()) {
			finishAlignedInputEnhancementFallback();
		}
	} else if (!bIsSpeech && bPreviousVoice && cleanupTailReady
			   && (!continuousTransmission || forceSpeechCleanupE2ERelease)) {
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
		m_speechCleanupE2ECurrentDrainSamples = 0;
#endif
		// The product processor is followed by OG Mumble's common Speex stage,
		// which owns one callback of existing latency. Append one uncounted frame
		// so its final buffered cleanup output is emitted before the terminator.
		m_speechCleanupTransmitDrain.begin(cleanupLatencySamples, 1);
		if (m_speechCleanupTransmitDrain.active()) {
			speechCleanupDrainStarted = true;
			bIsSpeech                 = true;
			m_inputGateOpen           = false;
			m_inputGateAttackFrames   = 0;
			m_inputGateReleaseFrames  = 0;
			iHoldFrames               = Global::get().s.iVoiceHold;
		}
	}

	if (captureVoiceDiagnostic) {
		const float rawPeakDb =
			dMaxMic > 0.0f ? std::max(20.0f * std::log10(dMaxMic / 32768.0f), -96.0f) : -96.0f;
		VoiceActivationDebugCapture::InputMetrics metrics;
		metrics.rawRmsDb            = dPeakMic;
		metrics.rawPeakDb           = rawPeakDb;
		metrics.processedRmsDb      = dPeakSignal;
		metrics.cleanRmsDb          = dPeakCleanMic;
		metrics.amplitudeLevel      = amplitudeLevel;
		metrics.speechProbability   = fSpeechProb;
		metrics.selectedLevel       = level;
		metrics.silenceThreshold    = Global::get().s.fVADmin;
		metrics.speechThreshold     = Global::get().s.fVADmax;
		metrics.vadSource           = static_cast< int >(Global::get().s.vsVAD);
		metrics.inputGateMode       = static_cast< int >(Global::get().s.inputGateMode);
		metrics.transmitMode        = static_cast< int >(Global::get().s.atTransmit);
		metrics.vadCandidate        = vadCandidate;
		metrics.gateAllowed         = gateAllowed;
		metrics.gateOpen            = m_inputGateOpen;
		metrics.acousticSpeech      = acousticSpeech;
		metrics.transmitting        = bIsSpeech;
		metrics.transmissionBlocked = transmissionBlocked;
		metrics.holdFrames          = iHoldFrames;
		metrics.gateAttackFrames    = m_inputGateAttackFrames;
		metrics.gateReleaseFrames   = m_inputGateReleaseFrames;
		diagnosticCapture.captureInputFrame(diagnosticRawInput.data(), iFrameSize, iSampleRate, metrics,
											diagnosticTimestampUs);
	}

	if (bIsSpeech) {
		iSilentFrames = 0;
	} else {
		iSilentFrames++;
		if (iSilentFrames > 500)
			iFrameCounter = 0;
	}
	updateAutoInputEnhancement(fSpeechProb, acousticSpeech);
	if (m_inputEnhancementProbation.observeFrame(10, probationSpeech, probationHealth)
		== Mumble::InputEnhancement::AutoV1::ProbationAction::Rollback) {
		m_inputEnhancementHealthyForUpdate.store(false, std::memory_order_relaxed);
		m_inputEnhancementAutoAdapt            = false;
		m_inputEnhancementAutoProfileSwitching = false;
	}
	if (calibrationTransmissionBlocked) {
		// Calibration is local-only. Drop any partial packet/utterance state and
		// return before encodeOpusFrame()/flushCheck(), including the usual voice
		// terminator path. vector::clear retains the preallocated PCM capacity.
		iBufferedFrames = 0;
		opusBuffer.clear();
		qlFrames.clear();
		bPreviousVoice = false;
		previousPTT    = false;
		iBitrate       = 0;
		m_voiceActivityTransmitting.store(false, std::memory_order_relaxed);
		m_voiceActivityBitrate.store(0, std::memory_order_relaxed);
		if (p) {
			p->setTalking(Settings::Passive);
		}
		return;
	}

	if (p) {
		if (!bIsSpeech)
			p->setTalking(Settings::Passive);
		else if (voiceTargetID == Mumble::Protocol::ReservedTargetIDs::REGULAR_SPEECH)
			p->setTalking(Settings::Talking);
		else
			p->setTalking(Settings::Shouting);
	}

	if (Global::get().uiSession != 0) {
		AudioOutputPtr ao = Global::get().ao;

		if (ao) {
			const bool treatAsPTT         = isPTT || previousPTT;
			const bool audioCueEnabledPTT = Global::get().s.audioCueEnabledPTT && treatAsPTT;
			const bool audioCueEnabledVAD =
				Global::get().s.audioCueEnabledVAD && Global::get().s.atTransmit == Settings::VAD && !treatAsPTT;
			const bool audioCueEnabled = audioCueEnabledPTT || audioCueEnabledVAD;

			const bool playAudioOnCue  = bIsSpeech && !bPreviousVoice && audioCueEnabled;
			const bool playAudioOffCue = !bIsSpeech && bPreviousVoice && audioCueEnabled;
			const bool stopActiveCue   = m_activeAudioCue && (playAudioOnCue || playAudioOffCue);

			if (stopActiveCue) {
				// Cancel active cue first, if there is any
				ao->invalidateToken(m_activeAudioCue);
				m_activeAudioCue = {};
			}

			if (playAudioOnCue) {
				m_activeAudioCue = ao->playSample(Global::get().s.qsTxAudioCueOn, Global::get().s.cueVolume);
			} else if (playAudioOffCue) {
				m_activeAudioCue = ao->playSample(Global::get().s.qsTxAudioCueOff, Global::get().s.cueVolume);
			}

			if (Global::get().s.bTxMuteCue && !Global::get().bPushToMute && !Global::get().s.bDeaf
				&& bTalkingWhenMuted) {
				if (!qetLastMuteCue.isValid() || qetLastMuteCue.elapsed() > MUTE_CUE_DELAY) {
					qetLastMuteCue.start();
					ao->playSample(Global::get().s.qsTxMuteCue, Global::get().s.cueVolume);
					emit doMuteCue();
				}
			}
		}
	}

	if (!bIsSpeech && !bPreviousVoice) {
		iBitrate = 0;
		m_voiceActivityBitrate.store(0, std::memory_order_relaxed);

		if (tIdle.elapsed< std::chrono::seconds >().count() > Global::get().s.iIdleTime) {
			activityState = ActivityStateIdle;
			tIdle.restart();
			if (Global::get().s.iaeIdleAction == Settings::Deafen && !Global::get().s.bDeaf) {
				emit doDeaf();
			} else if (Global::get().s.iaeIdleAction == Settings::Mute && !Global::get().s.bMute) {
				emit doMute();
			}
		}

		if (activityState == ActivityStateReturnedFromIdle) {
			activityState = ActivityStateActive;
			if (Global::get().s.iaeIdleAction != Settings::Nothing && Global::get().s.bUndoIdleActionUponActivity) {
				if (Global::get().s.iaeIdleAction == Settings::Deafen && Global::get().s.bDeaf) {
					emit doDeaf();
				} else if (Global::get().s.iaeIdleAction == Settings::Mute && Global::get().s.bMute) {
					emit doMute();
				}
			}
		}

		m_preprocessor.setAGCIncrement(0);
		return;
	} else {
		m_preprocessor.setAGCIncrement(12);
	}

	// Linearize the packet-producing portion against calibration start. begin()
	// waits for a path already inside this lease; a racing path observes the
	// closed gate on entry and discards every partial packet before Opus.
	auto discardCalibrationPacketPath = [this, p]() noexcept {
		iBufferedFrames = 0;
		opusBuffer.clear();
		qlFrames.clear();
		bPreviousVoice = false;
		previousPTT    = false;
		iBitrate       = 0;
		m_voiceActivityTransmitting.store(false, std::memory_order_relaxed);
		m_voiceActivityBitrate.store(0, std::memory_order_relaxed);
		if (p) {
			p->setTalking(Settings::Passive);
		}
	};
	CalibrationPacketPathLease calibrationPacketPath(m_inputEnhancementCalibrationTransmissionBlock);
	if (!calibrationPacketPath) {
		discardCalibrationPacketPath();
		return;
	}

	if (bIsSpeech && !bPreviousVoice) {
		bResetEncoder = true;
	}

	tIdle.restart();
	if (!m_inputEnhancementCalibrationTransmissionBlock.packetPathMayContinue()) {
		discardCalibrationPacketPath();
		return;
	}

	EncodingOutputBuffer buffer;
	Q_ASSERT(buffer.size() >= static_cast< size_t >(iAudioQuality / 100 * iAudioFrames / 8));

	assert(iFrameSize % iMicChannels == 0);
	const unsigned int samplesPerChannel = iFrameSize / iMicChannels;
	emit audioInputEncountered(psSource, samplesPerChannel, iMicChannels, SAMPLE_RATE, bIsSpeech);

	int len = 0;

	bool encoded = true;
	if (!selectCodec())
		return;

	assert(m_codec == Mumble::Protocol::AudioCodec::Opus);

	// Encode via Opus
	encoded = false;
	opusBuffer.insert(opusBuffer.end(), psSource, psSource + iFrameSize);
	++iBufferedFrames;

	if (!bIsSpeech || iBufferedFrames >= iAudioFrames) {
		if (iBufferedFrames < iAudioFrames) {
			// Stuff frame to framesize if speech ends and we don't have enough audio
			// this way we are guaranteed to have a valid framecount and won't cause
			// a codec configuration switch by suddenly using a wildly different
			// framecount per packet.
			const int missingFrames = iAudioFrames - iBufferedFrames;
			opusBuffer.insert(opusBuffer.end(), static_cast< std::size_t >(iFrameSize * missingFrames), 0);
			iBufferedFrames += missingFrames;
			iFrameCounter += missingFrames;
		}

		Q_ASSERT(iBufferedFrames == iAudioFrames);

		len = encodeOpusFrame(&opusBuffer[0], iBufferedFrames * iFrameSize, buffer);
		opusBuffer.clear();
		if (len <= 0) {
			iBitrate = 0;
			m_voiceActivityBitrate.store(0, std::memory_order_relaxed);
			qWarning() << "encodeOpusFrame failed" << iBufferedFrames << iFrameSize << len;
			iBufferedFrames = 0; // These are lost. Make sure not to mess up our sequence counter next flushCheck.
			return;
		}
		encoded = true;
	}

	if (encoded) {
		flushCheck(QByteArray(reinterpret_cast< char * >(&buffer[0]), len), !bIsSpeech, voiceTargetID);
	}

	if (!bIsSpeech) {
		iBitrate = 0;
		m_voiceActivityBitrate.store(0, std::memory_order_relaxed);
	}

	bPreviousVoice = bIsSpeech;
	m_voiceActivityTransmitting.store(bPreviousVoice, std::memory_order_relaxed);
	if (!speechCleanupDrainStarted
		&& !(speechCleanupDrainFrame.draining && !speechCleanupDrainFrame.terminator)) {
		previousPTT = isPTT;
	}
}

static void sendAudioFrame(std::span< const Mumble::Protocol::byte > encodedPacket) {
	ServerHandlerPtr sh = Global::get().serverHandlerSnapshot();
	if (sh) {
		sh->sendMessage(encodedPacket.data(), static_cast< int >(encodedPacket.size()));
	}
}

void AudioInput::flushCheck(const QByteArray &frame, bool terminator, std::int32_t voiceTargetID) {
	qlFrames << frame;

	if (!terminator && iBufferedFrames < iAudioFrames)
		return;

	Mumble::Protocol::AudioData audioData;
	audioData.targetOrContext = static_cast< std::uint32_t >(voiceTargetID);
	audioData.isLastFrame     = terminator;

	if (terminator && Global::get().iPrevTarget > 0) {
		// If we have been whispering to some target but have just ended, terminator will be true. However
		// in the case of whispering this means that we just released the whisper key so this here is the
		// last audio frame that is sent for whispering. The whisper key being released means that Global::get().iTarget
		// is reset to 0 by now. In order to send the last whisper frame correctly, we have to use
		// Global::get().iPrevTarget which is set to whatever Global::get().iTarget has been before its last change.

		audioData.targetOrContext = static_cast< std::uint32_t >(Global::get().iPrevTarget);

		// We reset Global::get().iPrevTarget as it has fulfilled its purpose for this whisper-action. It'll be set
		// accordingly once the client whispers for the next time.
		Global::get().iPrevTarget = 0;
	}
	if (Global::get().s.lmLoopMode == Settings::Server) {
		audioData.targetOrContext = Mumble::Protocol::ReservedTargetIDs::SERVER_LOOPBACK;
	}

	audioData.usedCodec = m_codec;

	int frames      = iBufferedFrames;
	iBufferedFrames = 0;

	audioData.frameNumber = static_cast< std::size_t >(iFrameCounter - frames);

	if (Global::get().s.bTransmitPosition && Global::get().pluginManager && !Global::get().bCenterPosition
		&& Global::get().pluginManager->fetchPositionalData()) {
		Position3D currentPos = Global::get().pluginManager->getPositionalData().getPlayerPos();

		audioData.position[0] = currentPos.x;
		audioData.position[1] = currentPos.y;
		audioData.position[2] = currentPos.z;

		audioData.containsPositionalData = true;
	}

	assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
	// In Opus mode we only expect a single frame per packet
	assert(qlFrames.size() == 1);

	audioData.payload = std::span< const Mumble::Protocol::byte >(
		reinterpret_cast< const Mumble::Protocol::byte * >(qlFrames[0].constData()),
		static_cast< std::size_t >(qlFrames[0].size()));

	{
		ServerHandlerPtr sh = Global::get().serverHandlerSnapshot();
		if (sh) {
			VoiceRecorderPtr recorder(sh->voiceRecorder());
			if (recorder) {
				recorder->getRecordUser().addFrame(audioData);
			}

			m_udpEncoder.setProtocolVersion(sh->protocolVersion());
		}
	}

	if (Global::get().s.lmLoopMode == Settings::Local) {
		// Only add audio data to local loop buffer
		LoopUser::lpLoopy.addFrame(audioData);
	} else {
		// Encode audio frame and send out
		std::span< const Mumble::Protocol::byte > encodedAudioPacket = m_udpEncoder.encodeAudioPacket(audioData);

		if (!encodedAudioPacket.empty()) {
			sendAudioFrame(encodedAudioPacket);
		}
	}

	qlFrames.clear();
}

bool AudioInput::isAlive() const {
	return isRunning();
}

void AudioInput::onUserMuteDeafStateChanged() {
	const ClientUser *user = qobject_cast< ClientUser * >(QObject::sender());
	updateUserMuteDeafState(user);
}

void AudioInput::updateUserMuteDeafState(const ClientUser *user) {
	bool bMuted = user->bSuppress || user->bSelfMute;
	if (bUserIsMuted != bMuted) {
		bUserIsMuted = bMuted;
		onUserMutedChanged();
	}
}

void AudioInput::onUserMutedChanged() {
}
