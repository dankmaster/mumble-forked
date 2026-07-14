// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOOUTPUTSPEECH_H_
#define MUMBLE_MUMBLE_AUDIOOUTPUTSPEECH_H_

#include <speex/speex_jitter.h>
#include <speex/speex_resampler.h>

#include <QtCore/QMutex>

#include "AudioOutputBuffer.h"
#include "AudioOutputCache.h"
#include "MumbleProtocol.h"
#include "SpeechCleanup.h"
#include "Settings.h"

#include <array>
#include <memory>
#include <mutex>
#include <vector>

class ClientUser;
class SpeechCleanupProcessor;
struct OpusDecoder;

class AudioOutputSpeech : public AudioOutputBuffer {
private:
	Q_OBJECT
	Q_DISABLE_COPY(AudioOutputSpeech)
protected:
	static std::mutex s_audioCachesMutex;
	static std::vector< AudioOutputCache > s_audioCaches;

	static void invalidateAudioOutputCache(void *maskedIndex);
	static std::size_t storeAudioOutputCache(const Mumble::Protocol::AudioData &audioData);

	unsigned int iAudioBufferSize;
	unsigned int iBufferOffset;
	unsigned int iBufferFilled;
	unsigned int iOutputSize;
	unsigned int iLastConsume;
	unsigned int iFrameSize;
	unsigned int iFrameSizePerChannel;
	unsigned int iSampleRate;
	unsigned int iMixerFreq;
	bool bLastAlive;
	bool bHasTerminator;

	float *fFadeIn;
	float *fFadeOut;
	float *fResamplerBuffer;

	SpeexResamplerState *srs;

	QMutex qmJitter;
	JitterBuffer *jbJitter;
	int iMissCount;

	OpusDecoder *opusState;

	QList< QByteArray > qlFrames;

	std::unique_ptr< SpeechCleanupProcessor > m_remoteSpeechCleanup;
	Mumble::SpeechCleanup::Selection m_remoteSpeechCleanupSelection = {};
	std::array< float, 5760 > m_remoteSpeechCleanupMonoBuffer = {};
	bool m_remoteSpeechCleanupRequested = false;
	bool m_remoteSpeechCleanupActive = false;
	bool m_remoteSpeechCleanupWasApplied = false;
	Settings::RemoteSpeechCleanupPreset m_remoteSpeechCleanupPreset = Settings::Normal;
	float m_remoteSpeechCleanupMixFactor = 0.65f;
	unsigned int m_remoteSpeechCleanupDrainSamplesRemaining = 0;
	unsigned int m_remoteSpeechCleanupDrainedSamples = 0;
	bool m_remoteSpeechCleanupDrainCompleted = false;
	bool m_outputEndKnown = false;
	unsigned int m_outputEndBufferOffset = 0;

	bool isEffectivelyDualMono(const float *samples, unsigned int sampleCount) const;
	bool applyRemoteSpeechCleanup(float *samples, unsigned int sampleCount);
	bool beginRemoteSpeechCleanupDrain() noexcept;
	Settings::TalkState talkStateForAudioContext(Mumble::Protocol::audio_context_t context) const;
	void updateTalkingStateFromAudioContext(Mumble::Protocol::audio_context_t context);

public:
	Mumble::Protocol::audio_context_t m_audioContext;
	Mumble::Protocol::AudioCodec m_codec;
	int iMissedFrames;
	ClientUser *p;

	/// Fetch and decode frames from the jitter buffer. Called in mix().
	///
	/// @param frameCount Number of frames to decode. frame means a bundle of one sample from each channel.
	virtual bool prepareSampleBuffer(unsigned int frameCount) Q_DECL_OVERRIDE;

	void addFrameToBuffer(const Mumble::Protocol::AudioData &audioData);

	/// @param systemMaxBufferSize maximum number of samples the system audio play back may request each time
	AudioOutputSpeech(ClientUser *, unsigned int freq, Mumble::Protocol::AudioCodec codec,
					  unsigned int systemMaxBufferSize);
	~AudioOutputSpeech() Q_DECL_OVERRIDE;

#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
	/// Read-only state used by the developer-only deterministic E2E harness.
	/// These accessors never create, reset, or run a cleanup model.
	bool remoteSpeechCleanupRequestedForE2E() const;
	const Mumble::SpeechCleanup::Selection &remoteSpeechCleanupSelectionForE2E() const;
	const SpeechCleanupProcessor *remoteSpeechCleanupProcessorForE2E() const;
	bool remoteSpeechCleanupActiveForE2E() const;
	bool remoteSpeechCleanupWasAppliedForE2E() const;
	Settings::RemoteSpeechCleanupPreset remoteSpeechCleanupPresetForE2E() const;
	float remoteSpeechCleanupMixFactorForE2E() const;
	unsigned int remoteSpeechCleanupDrainedSamplesForE2E() const;
	bool remoteSpeechCleanupDrainCompletedForE2E() const;
#endif
};

#endif // AUDIOOUTPUTSPEECH_H_
