// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOINPUT_H_
#define MUMBLE_MUMBLE_AUDIOINPUT_H_

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include <speex/speex_echo.h>
#include <speex/speex_resampler.h>

#include "Audio.h"
#include "AudioOutputToken.h"
#include "AudioPreprocessor.h"
#include "EchoCancelOption.h"
#include "InputEnhancement.h"
#include "InputEnhancementAuto.h"
#include "InputEnhancementCalibrationRuntime.h"
#include "InputEnhancementLightProcessor.h"
#include "MumbleProtocol.h"
#include "SpeechCleanup.h"
#include "SpeechCleanupTransmitDrain.h"
#include "Settings.h"
#include "Timer.h"

class AudioInput;
class SpeechCleanupProcessor;
class WebRTCAudioEchoCanceller;
struct OpusEncoder;

using AudioInputPtr = std::shared_ptr< AudioInput >;

/**
 * A chunk of audio data to process
 * This struct wraps pointers to two dynamically allocated arrays, containing
 * PCM samples of microphone and speaker readback data (for echo cancellation).
 * Does not handle pointer ownership, so you'll have to deallocate them yourself.
 */
struct AudioChunk {
	AudioChunk() : mic(nullptr), speaker(nullptr) {}
	explicit AudioChunk(short *mic) : mic(mic), speaker(nullptr) {}
	AudioChunk(short *mic, short *speaker) : mic(mic), speaker(speaker) {}
	bool empty() const { return mic == nullptr; }

	short *mic;     ///< Pointer to microphone samples
	short *speaker; ///< Pointer to speaker samples, nullptr if echo cancellation is disabled
};

/*
 * According to https://www.speex.org/docs/manual/speex-manual/node7.html
 * "It is important that, at any time, any echo that is present in the input
 * has already been sent to the echo canceller as echo_frame."
 * Thus, we artificially introduce a small lag in the microphone by means of
 * a queue, so as to be sure the speaker data always precedes the microphone.
 *
 * There are conflicting requirements for the queue:
 * - it has to be small enough not to cause a noticeable lag in the voice
 * - it has to be large enough not to force us to drop packets frequently
 *   when the addMic() and addEcho() callbacks are called in a jittery way
 * - its fill level must be controlled so it does not operate towards zero
 *   elements size, as this would not provide the lag required for the
 *   echo canceller to work properly.
 *
 * The current implementation uses a 5 elements queue, with a control
 * statemachine that introduces packet drops to control the fill level
 * to at least 2 (plus or minus one) and less than 4 elements.
 * With a 10ms chunk, this queue should introduce a ~20ms lag to the voice.
 */
class Resynchronizer {
public:
	/**
	 * Add a microphone sample to the resynchronizer queue
	 * The resynchronizer may decide to drop the sample, and in that case
	 * the pointer will be deallocated not lo leak memory
	 *
	 * \param mic pointer to a dynamically allocated  array with PCM data
	 */
	void addMic(short *mic);

	/**
	 * Add a speaker sample to the resynchronizer
	 * The resynchronizer may decide to drop the sample, and in that case
	 * the pointer will be deallocated not lo leak memory
	 *
	 * \param mic pointer to a dynamically allocated array with PCM data
	 * \return If microphone data is available, the resynchronizer will return a
	 * valid audio chunk to encode, otherwise an empty chunk will be returned
	 */
	AudioChunk addSpeaker(short *speaker);

	/**
	 * Reinitialize the resynchronizer, emptying the queue in the process.
	 */
	void reset();

	/**
	 * \return the nominal lag that the resynchronizer tries to enforce on the
	 * microphone data, in order to make sure the speaker data is always passed
	 * first to the echo canceller
	 */
	int getNominalLag() const { return 2; }

	~Resynchronizer();

	bool bDebugPrintQueue = false; ///< Enables printing queue fill level stats

private:
	/**
	 * Print queue level stats for debugging purposes
	 * \param mic used to distinguish between addMic() and addSpeaker()
	 */
	void printQueue(char who);

	// TODO: there was a mutex (qmEcho), but can the callbacks be called concurrently?
	mutable std::mutex m;
	std::list< short * > micQueue;                          ///< Queue of microphone samples
	enum { S0, S1a, S1b, S2, S3, S4a, S4b, S5 } state = S0; ///< Queue fill control statemachine
};

class AudioInputRegistrar {
private:
	Q_DISABLE_COPY(AudioInputRegistrar)
public:
	static QMap< QString, AudioInputRegistrar * > *qmNew;
	static QString current;
	static AudioInputPtr newFromChoice(QString choice = QString());

	const QString name;
	int priority;

	/// A list of echo cancellation options available for this backend.
	std::vector< EchoCancelOptionID > echoOptions;

	AudioInputRegistrar(const QString &n, int priority = 0);
	virtual ~AudioInputRegistrar();
	virtual AudioInput *create()                               = 0;
	virtual const QVariant getDeviceChoice()                   = 0;
	virtual const QList< audioDevice > getDeviceChoices()      = 0;
	virtual void setDeviceChoice(const QVariant &, Settings &) = 0;

	/// Resolve the configured choice to the device identity that will be opened.
	/// Backends that can resolve an OS default to a physical endpoint should
	/// override this method. The generic implementation keeps unresolved/default
	/// and PipeWire identities session-only.
	virtual Mumble::InputEnhancement::DeviceIdentity resolveDeviceIdentity();
	virtual Mumble::InputEnhancement::DeviceIdentity resolveDeviceIdentity(const Settings &settings);
	static Mumble::InputEnhancement::DeviceIdentity resolveDeviceIdentity(const QString &backend,
															 const Settings &settings);
	static Mumble::InputEnhancement::DeviceIdentity resolveCurrentDeviceIdentity();

	/// Check that given combination of echoOption and outputSystem combination is suitable for echo cancellation
	virtual bool canEcho(EchoCancelOptionID echoOptionId, const QString &outputSystem) const = 0;
	virtual bool canExclusive() const;

	/**
	 * Check if Mumble's microphone access has been denied by the OS.
	 * Both Windows and macOS have builtin privacy safeguards that display a message asking for users'
	 * consent when apps are trying to use the microphone, and/or provide ways to deny the microphone
	 * access of some apps.
	 * This function should check if Mumble has the permission to use the microphone.
	 * Note: It is possible that this result could only be known after trying to initialize the audio backend.
	 * Generally, call this function after attempts to initialize the AudioInput have been made.
	 * @return true if microphone access is denied.
	 */
	virtual bool isMicrophoneAccessDeniedByOS() = 0;
};

class AudioInput : public QThread {
	friend class AudioNoiseWidget;
	friend class AudioEchoWidget;
	friend class AudioStats;

private:
	Q_OBJECT
	Q_DISABLE_COPY(AudioInput)
protected:
	typedef enum { SampleShort, SampleFloat } SampleFormat;
	typedef void (*inMixerFunc)(float *RESTRICT, const void *RESTRICT, unsigned int, unsigned int, quint64);

private:
	bool bDebugDumpInput;                           ///< When true, dump pcm data to debug the echo canceller
	std::ofstream outMic, outSpeaker, outProcessed; ///< Files to dump raw pcm data

	SpeexResamplerState *srsMic, *srsEcho;

	std::unique_ptr< Mumble::Protocol::byte[] > m_legacyBuffer;
	Mumble::Protocol::UDPAudioEncoder< Mumble::Protocol::Role::Client > m_udpEncoder;

	unsigned int iMicFilled, iEchoFilled;
	inMixerFunc imfMic, imfEcho;
	inMixerFunc chooseMixer(const unsigned int nchan, SampleFormat sf, quint64 mask);
	void resetAudioProcessor();

	OpusEncoder *opusState;
	Mumble::InputEnhancement::DeviceIdentity m_inputDeviceIdentity;
	mutable QMutex m_inputDeviceIdentityMutex;
	std::unique_ptr< Mumble::InputEnhancement::Pipeline > m_inputEnhancementPipeline;
	/// Auto owns all neural candidates in a lock-free handoff bank. Retired
	/// processors are reconstructed by its lifecycle worker, never by the audio
	/// callback.
	std::unique_ptr< Mumble::InputEnhancement::AutoV1::PreparedPipelineBank >
		m_inputEnhancementAutoPipelineBank;
	Mumble::InputEnhancement::Engine m_inputEnhancementEngine = Mumble::InputEnhancement::Engine::None;
	Mumble::InputEnhancement::Profile m_inputEnhancementConcreteProfile = Mumble::InputEnhancement::Profile::Original;
	Mumble::InputEnhancement::CpuClass m_inputEnhancementCpuClass = Mumble::InputEnhancement::CpuClass::Standard;
	Mumble::InputEnhancement::AutoV1::Policy m_inputEnhancementAutoPolicy;
	Mumble::InputEnhancement::AutoV1::ObservationTracker m_inputEnhancementAutoTracker;
	Mumble::InputEnhancement::AutoV1::SafeProfileSwitchGate m_inputEnhancementAutoSwitchGate;
	Mumble::InputEnhancement::AutoV1::AcousticSilenceSwitchBoundary
		m_inputEnhancementAutoSilenceBoundary;
	Mumble::InputEnhancement::Profile m_inputEnhancementLastWorkingProfile =
		Mumble::InputEnhancement::Profile::Original;
	bool m_inputEnhancementRuntimeRecoveryPending = false;
	std::atomic_bool m_inputEnhancementHealthyForUpdate        = true;
	std::unique_ptr< Mumble::InputEnhancement::CalibrationRuntimeBridge >
		m_inputEnhancementCalibrationRuntime;
	Mumble::InputEnhancement::DefaultPreference m_inputEnhancementCalibrationBaselinePreference;
	std::optional< Mumble::InputEnhancement::RecipeBinding > m_inputEnhancementCalibrationBaselineBinding;
	bool m_inputEnhancementCalibrationBaselineAvailable = false;
	std::atomic< Mumble::InputEnhancement::CalibrationRuntimeBridge * >
		m_inputEnhancementCalibrationForCallback { nullptr };
	Mumble::InputEnhancement::CalibrationTransmissionBlock m_inputEnhancementCalibrationTransmissionBlock;
	Mumble::InputEnhancement::InputEnhancementProbationController m_inputEnhancementProbation;
	QTimer m_inputEnhancementProbationServiceTimer;
	bool m_usesLegacyInputEnhancement                        = false;
	bool m_inputEnhancementAutoAdapt                         = false;
	bool m_inputEnhancementAutoProfileSwitching              = false;
	int m_inputEnhancementBaseReduction                     = 50;
	int m_inputEnhancementBaseCharacter                     = 50;
	int m_inputEnhancementActiveReduction                   = 50;
	int m_inputEnhancementActiveCharacter                   = 50;
	int m_inputEnhancementSpeexStrength                      = -30;
	Mumble::InputEnhancement::LightProcessor m_inputEnhancementLightProcessor;
	Settings::NoiseCancel m_configuredLegacyNoiseCancelMode = Settings::NoiseCancelOff;
	std::unique_ptr< SpeechCleanupProcessor > m_speechCleanupProcessor;
	Mumble::SpeechCleanup::Selection m_speechCleanupSelection = {};
	Mumble::SpeechCleanup::TransmitDrain m_speechCleanupTransmitDrain;
	std::unique_ptr< WebRTCAudioEchoCanceller > m_webrtcEchoCanceller;
	bool selectCodec();
	void initializeInputEnhancement();
	void initializeInputEnhancementProbation(
		const Mumble::InputEnhancement::DeviceProfileState *deviceProfile);
	void failInputEnhancementProbation(
		Mumble::InputEnhancement::ProbationHealthSignal signal) noexcept;
	Mumble::InputEnhancement::Pipeline *activeInputEnhancementPipeline() noexcept;
	const Mumble::InputEnhancement::Pipeline *activeInputEnhancementPipeline() const noexcept;
	bool neuralInputEnhancementReady() const noexcept;
	bool alignedInputEnhancementFallbackActive() const noexcept;
	void finishAlignedInputEnhancementFallback() noexcept;
	unsigned int inputEnhancementLatencySamples() const noexcept;
	bool processInputEnhancementFrame(std::array< float, Mumble::InputEnhancement::frameSamples > &frame,
									 unsigned int validSamples = Mumble::InputEnhancement::frameSamples) noexcept;
	Mumble::InputEnhancement::AutoV1::DeadlinePressure inputEnhancementDeadlinePressure() const noexcept;
	bool autoInputEnhancementCandidatePrepared(Mumble::InputEnhancement::Profile profile) const noexcept;
	bool applyPreparedAutoInputEnhancementProfile(Mumble::InputEnhancement::Profile profile) noexcept;
	void updateAutoInputEnhancement(float vadConfidence, bool acousticSpeech) noexcept;
	void selectNoiseCancel();

	using EncodingOutputBuffer = std::array< unsigned char, 1275 >;

	int encodeOpusFrame(short *source, int size, EncodingOutputBuffer &buffer);

	QElapsedTimer qetLastMuteCue;

	AudioOutputToken m_activeAudioCue;

protected:
	Mumble::Protocol::AudioCodec m_codec;
	SampleFormat eMicFormat, eEchoFormat;
	/// Audio-thread-only diagnostics used by the opt-in deterministic E2E backend.
	const SpeechCleanupProcessor *speechCleanupProcessorForDiagnostics() const noexcept;
	const Mumble::SpeechCleanup::Selection &speechCleanupSelectionForDiagnostics() const noexcept;
	bool speechCleanupReadyForDiagnostics() const noexcept;
	QString speechCleanupActiveModelIdForDiagnostics() const;
	QString speechCleanupActiveModelPathForDiagnostics() const;
	bool speechCleanupUsedFallbackForDiagnostics() const;
	unsigned int speechCleanupLatencyForDiagnostics() const noexcept;
	/// Record the endpoint that the backend actually opened. If it differs from
	/// the identity used for pre-warming, enhancement fails closed to Original;
	/// model construction is never moved onto the audio thread.
	void confirmOpenedInputDeviceIdentity(Mumble::InputEnhancement::DeviceIdentity identity) noexcept;
	/// Touch the already confirmed per-device profile only after the backend has
	/// fully started its capture chain.
	void markOpenedInputDeviceProfileUsed() noexcept;
	const Mumble::InputEnhancement::Pipeline *inputEnhancementPipelineForDiagnostics() const noexcept;
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
	bool usesLegacyInputEnhancementForDiagnostics() const noexcept;
	Mumble::InputEnhancement::Profile inputEnhancementProfileForDiagnostics() const noexcept;
	std::uint64_t inputEnhancementModelInitializationAttemptsForDiagnostics() const noexcept;
	/// Releases the current deterministic transmission, drains any causal
	/// sender cleanup latency, and emits a real encoded Opus terminator without
	/// mutating the process-wide transmit-mode setting.
	unsigned int finishSpeechCleanupE2ETransmission();
#endif

	unsigned int iMicChannels, iEchoChannels;
	unsigned int iMicFreq, iEchoFreq;
	unsigned int iMicLength, iEchoLength;
	unsigned int iMicSampleSize, iEchoSampleSize;
	unsigned int iEchoMCLength, iEchoFrameSize;
	quint64 uiMicChannelMask, uiEchoChannelMask;

	bool bEchoMulti;
	Settings::NoiseCancel noiseCancel;
	// Standard microphone sample rate (samples/s)
	static const unsigned int iSampleRate = SAMPLE_RATE;
	/// Based the sample rate, 48,000 samples/s = 48 samples/ms.
	/// For each 10 ms, this yields 480 samples. This corresponds numerically with the calculation:
	/// iFrameSize = 48000 / 100 = 480 samples, allowing a consistent 10ms of audio data per frame.
	static const int iFrameSize = SAMPLE_RATE / 100;

	QMutex qmSpeex;
	AudioPreprocessor m_preprocessor;
	SpeexEchoState *sesEcho;

	/// bResetEncoder is a flag that notifies
	/// our encoder functions that the encoder
	/// needs to be reset.
	bool bResetEncoder;

	/// Encoded audio rate in bit/s
	int iAudioQuality;
	bool bAllowLowDelay;
	/// Number of 10ms audio "frames" per packet (!= frames in packet)
	int iAudioFrames;

	/// The minimum time in ms that has to pass between the playback of two consecutive mute cues.
	static constexpr unsigned int MUTE_CUE_DELAY = 5000;

	float *pfMicInput;
	float *pfEchoInput;

	Resynchronizer resync;
	std::vector< short > opusBuffer;

	void encodeAudioFrame(AudioChunk chunk);
	void addMic(const void *data, unsigned int nsamp);
	void addEcho(const void *data, unsigned int nsamp);
	bool inputGateAllowsSpeech(bool candidateSpeech, float amplitudeLevel, float speechProbability);

	volatile bool bRunning;
	volatile bool bPreviousVoice;
	volatile bool previousPTT;

	int iFrameCounter;
	int iSilentFrames;
	int iHoldFrames;
	bool m_inputGateOpen;
	int m_inputGateAttackFrames;
	int m_inputGateReleaseFrames;
#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
	bool m_forceSpeechCleanupE2ERelease = false;
	unsigned int m_speechCleanupE2ECurrentDrainSamples = 0;
	unsigned int m_speechCleanupE2ELastCompletedDrainSamples = 0;
	std::atomic< std::uint64_t > m_speechCleanupE2EModelInitializationAttempts { 0 };
#endif
	int iBufferedFrames;

	QList< QByteArray > qlFrames;
	void flushCheck(const QByteArray &, bool terminator, std::int32_t voiceTargetID);

	void initializeMixer();

	static void adjustBandwidth(int bitspersec, int &bitrate, int &frames, bool &allowLowDelay);

	bool bUserIsMuted;

signals:
	void doDeaf();
	void doMute();
	void doMuteCue();
	/// A signal emitted if audio input is being encountered
	///
	/// @param inputPCM The encountered input PCM
	/// @param sampleCount The amount of samples in the input
	/// @param channelCount The amount of channels in the input
	/// @param sampleRate The used sample rate in Hz
	/// @param isSpeech Whether Mumble considers the input to be speech
	void audioInputEncountered(short *inputPCM, unsigned int sampleCount, unsigned int channelCount,
							   unsigned int sampleRate, bool isSpeech);

public:
	enum class InputEnhancementCalibrationStartError : std::uint8_t {
		None,
		AlreadyRunning,
		AutoUnsupported,
		LegacyOverrideActive,
		ActiveRecipeUnhealthy,
		MissingExactActiveRecipe,
		RuntimeRejected
	};

	typedef enum { ActivityStateIdle, ActivityStateReturnedFromIdle, ActivityStateActive } ActivityState;

	// Race-free cross-thread telemetry for presentation surfaces. Individual fields may
	// originate from adjacent audio frames; consumers must not treat this as DSP state.
	struct VoiceActivitySnapshot {
		float peakSignalDb      = 0.0f;
		float peakCleanMicDb    = 0.0f;
		float speechProbability = 0.0f;
		int bitrate             = 0;
		bool transmitting       = false;

		float amplitudeLevel() const;
		bool hasProcessedInput() const;
	};

	ActivityState activityState;

	bool bResetProcessor;

	Timer tIdle;

	int iBitrate;
	float dPeakSpeaker, dPeakSignal, dMaxMic, dPeakMic, dPeakCleanMic;
	float fSpeechProb;

	float amplitudeVoiceActivityLevel() const;
	float voiceActivityLevel() const;
	static float voiceActivityLevelFor(Settings::VADSource source, float amplitudeLevel, float speechProbability);
	static bool voiceActivityTriggers(float level, float silenceThreshold, float speechThreshold, bool wasTransmitting);
	static bool inputGateAllowsSpeechFor(Settings::InputGateMode mode, bool candidateSpeech, float amplitudeLevel,
										 float speechProbability, bool &gateOpen, int &attackFrames,
										 int &releaseFrames);

	static int clampFramesPerPacket(int frames);
	static int packetDurationMsForFrames(int frames);
	static int opusMaxAudioBitrateForFrames(int frames);
	static int maxAudioBitrateForConfiguration(int serverBandwidth, int frames, bool experimentalHighBitrateEnabled,
											   bool transmitPosition, bool tcpMode);
	static int getNetworkBandwidth(int bitrate, int frames);
	static void setMaxBandwidth(int bitspersec);

	/// Construct an AudioInput.
	///
	/// This constructor is only ever called by Audio::startInput(), and is guaranteed
	/// to be called on the application's main thread.
	AudioInput();

	/// Destroy an AudioInput.
	///
	/// This destructor is only ever called by Audio::stopInput() and Audio::stop(),
	/// and is guaranteed to be called on the application's main thread.
	~AudioInput() Q_DECL_OVERRIDE;
	void run() Q_DECL_OVERRIDE = 0;
	virtual bool isAlive() const;
	bool isTransmitting() const;
	VoiceActivitySnapshot voiceActivitySnapshot() const noexcept;
	/// The physical/session microphone identity selected for this input object.
	/// This is safe to query while the backend performs its startup handshake.
	Mumble::InputEnhancement::DeviceIdentity inputDeviceIdentity() const;
	/// Thread-safe startup/rollback health signal. Original and Light are
	/// healthy without a model; a requested neural recipe is unhealthy after
	/// any initialization or runtime fallback.
	bool inputEnhancementHealthyForUpdate() const noexcept;
	/// Starts a local-only capture session. Allocation and model/Opus setup are
	/// completed here on the control thread before the bridge is published to
	/// the audio callback. candidateControls only shape the recipes under test;
	/// rollback always binds to the exact healthy recipe this AudioInput is
	/// actually running.
	bool startInputEnhancementCalibration(
		const Mumble::InputEnhancement::DefaultPreference &candidateControls,
		bool captureOptionalLocalNoise, std::uint64_t blindSeed,
		InputEnhancementCalibrationStartError *error = nullptr);
	Mumble::InputEnhancement::CalibrationRuntimeBridge *inputEnhancementCalibrationRuntime() noexcept;
	/// Revalidates the selected calibration recipe against the current device,
	/// CPU capability, and package catalog immediately before persisting it.
	bool applyInputEnhancementCalibration(Mumble::InputEnhancement::Settings &settings, qint64 nowEpochMs);
	/// Clear the independent final-encode block only after a terminal runtime
	/// transition has quiesced capture callbacks.
	void synchronizeInputEnhancementCalibrationTransmissionBlock() noexcept;
	Mumble::InputEnhancement::ProbationSettingsResult serviceInputEnhancementProbation();
	bool undoInputEnhancementProbationRollback(Mumble::InputEnhancement::Settings &settings);
	bool inputEnhancementProbationRunning() const noexcept;
	bool inputEnhancementProbationUndoAvailable() const noexcept;

	void updateUserMuteDeafState(const ClientUser *user);

protected:
	virtual void onUserMutedChanged();

private:
	std::atomic< float > m_voiceActivityPeakSignalDb { 0.0f };
	std::atomic< float > m_voiceActivityPeakCleanMicDb { 0.0f };
	std::atomic< float > m_voiceActivitySpeechProbability { 0.0f };
	std::atomic< int > m_voiceActivityBitrate { 0 };
	std::atomic< bool > m_voiceActivityTransmitting { false };

public slots:
	void onUserMuteDeafStateChanged();
};

#endif
