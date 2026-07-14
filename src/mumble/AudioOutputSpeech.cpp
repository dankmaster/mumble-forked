// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioOutputSpeech.h"

#include "Audio.h"
#include "ClientUser.h"
#include "PacketDataStream.h"
#include "SpeechCleanup.h"
#include "SpeechCleanupProcessor.h"
#include "Utils.h"
#include "Global.h"

#include <opus.h>

#include <algorithm>
#include <cassert>
#include <cmath>

std::mutex AudioOutputSpeech::s_audioCachesMutex;
std::vector< AudioOutputCache > AudioOutputSpeech::s_audioCaches(100);

namespace {
	constexpr unsigned int REMOTE_SPEECH_CLEANUP_MAX_MONO_SAMPLES = (SAMPLE_RATE * 120) / 1000;
	constexpr float REMOTE_SPEECH_CLEANUP_STEREO_EPSILON    = 1.0e-4f;

	Mumble::SpeechCleanup::Selection currentRemoteSpeechCleanupSelection() {
		return Mumble::SpeechCleanup::normalizeSelection({
			Global::get().s.remoteSpeechCleanupBackend,
			Global::get().s.remoteSpeechCleanupModelId,
			Global::get().s.remoteSpeechCleanupCustomModelPath,
		});
	}

	float remoteSpeechCleanupMixFactor(Settings::RemoteSpeechCleanupPreset preset) {
		switch (preset) {
			case Settings::Light:
				return 0.35f;
			case Settings::Normal:
				return 0.65f;
			case Settings::Aggressive:
				return 1.0f;
			default:
				return 0.65f;
		}
	}

	constexpr unsigned int fullSpanFadeIndex(unsigned int position, unsigned int span,
									 unsigned int tableSize) {
		return span > 1 ? (position * (tableSize - 1)) / (span - 1) : tableSize - 1;
	}

	static_assert(fullSpanFadeIndex(0, 480, 480) == 0);
	static_assert(fullSpanFadeIndex(479, 480, 480) == 479);
	static_assert(fullSpanFadeIndex(335, 336, 480) == 479);
	static_assert(fullSpanFadeIndex(0, 1, 480) == 479);
} // namespace

void AudioOutputSpeech::invalidateAudioOutputCache(void *maskedIndex) {
	// The given "pointer" actually is to be understood as an index
	const std::size_t index = reinterpret_cast< std::size_t >(maskedIndex) - 1;

	std::lock_guard< std::mutex > lock(s_audioCachesMutex);

	if (index < s_audioCaches.size()) {
		s_audioCaches[index].clear();
	}
}

std::size_t AudioOutputSpeech::storeAudioOutputCache(const Mumble::Protocol::AudioData &audioData) {
	std::lock_guard< std::mutex > lock(s_audioCachesMutex);

	// Find free spot in s_audioCaches
	auto it = std::find_if(s_audioCaches.begin(), s_audioCaches.end(),
						   [](const AudioOutputCache &chunk) { return !chunk.isValid(); });

	if (it != s_audioCaches.end()) {
		// Write audio data to that free (currently unused) chunk
		it->loadFrom(audioData);

		return static_cast< std::size_t >(std::distance(s_audioCaches.begin(), it));
	} else {
		// The list of audio chunks is full -> extend it
		AudioOutputCache chunk;
		chunk.loadFrom(audioData);

		s_audioCaches.push_back(std::move(chunk));

		return s_audioCaches.size() - 1;
	}
}


AudioOutputSpeech::AudioOutputSpeech(ClientUser *user, unsigned int freq, Mumble::Protocol::AudioCodec codec,
									 unsigned int systemMaxBufferSize)
	: iMixerFreq(freq), m_codec(codec), p(user) {
	int err;

	opusState = nullptr;

	bHasTerminator = false;
	bStereo        = false;

	iSampleRate = SAMPLE_RATE;

	// opus's "frame" means different from normal audio term "frame"
	// normally, a frame means a bundle of only one sample from each channel,
	// e.g. for a stereo stream, ...[LR]LRLRLR.... where the bracket indicates a frame
	// in opus term, a frame means samples that span a period of time, which can be either stereo or mono
	// e.g. ...[LRLR....LRLR].... or ...[MMMM....MMMM].... for mono stream
	// opus supports frames with: 2.5, 5, 10, 20, 40 or 60 ms of audio data.
	// sample rate / 100 means 10ms (0.01s) mono audio data points (samples) per frame.
	iFrameSizePerChannel = iFrameSize = iSampleRate / 100; // for mono stream

	assert(m_codec == Mumble::Protocol::AudioCodec::Opus);

	// Always pretend Stereo mode is true by default. since opus will convert mono stream to stereo stream.
	// https://tools.ietf.org/html/rfc6716#section-2.1.2
	bStereo   = true;
	opusState = opus_decoder_create(static_cast< int >(iSampleRate), bStereo ? 2 : 1, nullptr);
	opus_decoder_ctl(opusState,
					 OPUS_SET_PHASE_INVERSION_DISABLED(1)); // Disable phase inversion for better mono downmix.

	// Remote cleanup is an utterance-scoped processor. Settings and the per-user
	// enable decision are snapshotted here, off the mixer thread, and changes made
	// while somebody is speaking deliberately take effect on their next utterance.
	// This avoids model creation/reload and state discontinuities in the callback.
	m_remoteSpeechCleanupSelection = currentRemoteSpeechCleanupSelection();
	m_remoteSpeechCleanupRequested = user && user->isRemoteSpeechCleanupEnabled();
	m_remoteSpeechCleanupPreset    = Global::get().s.remoteSpeechCleanupPreset;
	m_remoteSpeechCleanupMixFactor = remoteSpeechCleanupMixFactor(m_remoteSpeechCleanupPreset);
	if (m_remoteSpeechCleanupRequested) {
		m_remoteSpeechCleanup       = createSpeechCleanupProcessor(m_remoteSpeechCleanupSelection);
		m_remoteSpeechCleanupActive = m_remoteSpeechCleanup && m_remoteSpeechCleanup->isReady();
	}

	// iAudioBufferSize: size (in unit of float) of the buffer used to store decoded pcm data.
	// For opus, the maximum frame size of a packet is 120ms (the maximum duration for a single frame
	// is 60ms but multiple frames may be bundled into a single packet of a duration up to 120ms).
	iAudioBufferSize = iSampleRate * 120 / 1000; // = SampleRate * 120ms = 48000Hz * 0.12s = 5760, ~23KB

	// iBufferSize: size of the buffer to store the resampled audio data.
	// Note that the number of samples in each opus packet can be different from the number of samples the system
	// requests from us each time (this is known as the system's audio buffer size).
	// For example, the maximum size of an opus packet is 120ms, but the system's audio buffer size is typically
	// ~5ms on my laptop.
	// Whenever the system's audio callback is called, we have two choice:
	//  1. Decode a new opus packet. Then we need a buffer to store unused samples (which don't fit in the system's
	//  buffer),
	//  2. Use unused samples from the buffer (remaining from the last decoded frame).
	// How large should this buffer be? Consider the case in which remaining samples in the buffer can not fill
	// the system's audio buffer. In that case, we need to decode a new opus packet. In the worst case, the buffer size
	// needed is
	//    120ms of new decoded audio data + system's buffer size - 1.
	iOutputSize = static_cast< unsigned int >(
		ceilf(static_cast< float >(iAudioBufferSize * iMixerFreq) / static_cast< float >(iSampleRate)));
	iBufferSize = iOutputSize + systemMaxBufferSize; // -1 has been rounded up

	if (bStereo) {
		iAudioBufferSize *= 2;
		iOutputSize *= 2;
		iBufferSize *= 2;
		iFrameSize *= 2;
	}

	pfBuffer = new float[iBufferSize];

	srs              = nullptr;
	fResamplerBuffer = nullptr;
	if (iMixerFreq != iSampleRate) {
		srs              = speex_resampler_init(bStereo ? 2 : 1, iSampleRate, iMixerFreq, 3, &err);
		fResamplerBuffer = new float[iAudioBufferSize];
	}

	iBufferOffset = iBufferFilled = iLastConsume = 0;
	bLastAlive                                   = true;

	iMissCount    = 0;
	iMissedFrames = 0;

	m_audioContext = Mumble::Protocol::AudioContext::INVALID;

	jbJitter   = jitter_buffer_init(static_cast< int >(iFrameSize));
	int margin = Global::get().s.iJitterBufferSize * static_cast< int >(iFrameSize);
	jitter_buffer_ctl(jbJitter, JITTER_BUFFER_SET_MARGIN, &margin);

	// We are configuring our Jitter buffer to use a custom deleter function. This prevents the buffer from
	// copying the stored data into the buffer itself and also from releasing the memory of it. Instead it
	// will now call this "deleter" function instead.
	// This allows us to manage our own (global) storage for our audio data. With that, we can reuse the same
	// memory regions in order to avoid frequent memory allocations and deallocations.
	// Also this is the basis for using our trick of actually only storing indices instead of proper data
	// pointers in the buffer.
	jitter_buffer_ctl(jbJitter, JITTER_BUFFER_SET_DESTROY_CALLBACK,
					  reinterpret_cast< void * >(&AudioOutputSpeech::invalidateAudioOutputCache));

	fFadeIn  = new float[iFrameSizePerChannel];
	fFadeOut = new float[iFrameSizePerChannel];

	float mul = static_cast< float >(M_PI / (2.0 * static_cast< double >(iFrameSizePerChannel)));
	for (unsigned int i = 0; i < iFrameSizePerChannel; ++i)
		fFadeIn[i] = fFadeOut[iFrameSizePerChannel - i - 1] = sinf(static_cast< float >(i) * mul);
}

AudioOutputSpeech::~AudioOutputSpeech() {
	if (opusState) {
		opus_decoder_destroy(opusState);
	}

	if (srs)
		speex_resampler_destroy(srs);

	jitter_buffer_destroy(jbJitter);

	if (p) {
		p->setTalking(Settings::Passive);
	}

	delete[] fFadeIn;
	delete[] fFadeOut;
	delete[] fResamplerBuffer;
}

bool AudioOutputSpeech::isEffectivelyDualMono(const float *samples, unsigned int sampleCount) const {
	if (!samples || !bStereo || sampleCount < 2 || sampleCount % 2 != 0) {
		return false;
	}

	for (unsigned int i = 0; i < sampleCount; i += 2) {
		if (std::fabs(samples[i] - samples[i + 1]) > REMOTE_SPEECH_CLEANUP_STEREO_EPSILON) {
			return false;
		}
	}

	return true;
}

bool AudioOutputSpeech::applyRemoteSpeechCleanup(float *samples, unsigned int sampleCount) {
	if (!samples || !m_remoteSpeechCleanupRequested || !m_remoteSpeechCleanupActive
		|| !m_remoteSpeechCleanup || !bStereo || sampleCount == 0
		|| !isEffectivelyDualMono(samples, sampleCount)) {
		return false;
	}

	const unsigned int samplesPerChannel = sampleCount / 2;
	if (samplesPerChannel > REMOTE_SPEECH_CLEANUP_MAX_MONO_SAMPLES) {
		return false;
	}

	for (unsigned int i = 0; i < samplesPerChannel; ++i) {
		m_remoteSpeechCleanupMonoBuffer[i] = samples[2 * i];
	}

	m_remoteSpeechCleanup->processInPlace(m_remoteSpeechCleanupMonoBuffer.data(), samplesPerChannel,
										  m_remoteSpeechCleanupMixFactor);
	m_remoteSpeechCleanupWasApplied = true;

	for (unsigned int i = 0; i < samplesPerChannel; ++i) {
		samples[2 * i]     = m_remoteSpeechCleanupMonoBuffer[i];
		samples[2 * i + 1] = m_remoteSpeechCleanupMonoBuffer[i];
	}

	return true;
}

bool AudioOutputSpeech::beginRemoteSpeechCleanupDrain() noexcept {
	if (m_remoteSpeechCleanupDrainSamplesRemaining > 0) {
		return true;
	}
	if (!m_remoteSpeechCleanupWasApplied || !m_remoteSpeechCleanup) {
		return false;
	}

	const unsigned int cleanupLatency = m_remoteSpeechCleanup->latencySamples();
	if (cleanupLatency == 0) {
		return false;
	}

	m_remoteSpeechCleanupDrainSamplesRemaining = cleanupLatency;
	m_remoteSpeechCleanupDrainCompleted        = false;
	return true;
}

#ifdef MUMBLE_HAS_SPEECH_CLEANUP_E2E
bool AudioOutputSpeech::remoteSpeechCleanupRequestedForE2E() const {
	return m_remoteSpeechCleanupRequested;
}

const Mumble::SpeechCleanup::Selection &AudioOutputSpeech::remoteSpeechCleanupSelectionForE2E() const {
	return m_remoteSpeechCleanupSelection;
}

const SpeechCleanupProcessor *AudioOutputSpeech::remoteSpeechCleanupProcessorForE2E() const {
	return m_remoteSpeechCleanup.get();
}

bool AudioOutputSpeech::remoteSpeechCleanupActiveForE2E() const {
	return m_remoteSpeechCleanupActive;
}

bool AudioOutputSpeech::remoteSpeechCleanupWasAppliedForE2E() const {
	return m_remoteSpeechCleanupWasApplied;
}

Settings::RemoteSpeechCleanupPreset AudioOutputSpeech::remoteSpeechCleanupPresetForE2E() const {
	return m_remoteSpeechCleanupPreset;
}

float AudioOutputSpeech::remoteSpeechCleanupMixFactorForE2E() const {
	return m_remoteSpeechCleanupMixFactor;
}

unsigned int AudioOutputSpeech::remoteSpeechCleanupDrainedSamplesForE2E() const {
	return m_remoteSpeechCleanupDrainedSamples;
}

bool AudioOutputSpeech::remoteSpeechCleanupDrainCompletedForE2E() const {
	return m_remoteSpeechCleanupDrainCompleted;
}
#endif

Settings::TalkState AudioOutputSpeech::talkStateForAudioContext(Mumble::Protocol::audio_context_t context) const {
	switch (context) {
		case Mumble::Protocol::AudioContext::LISTEN:
			// Fallthrough
		case Mumble::Protocol::AudioContext::NORMAL:
			return Settings::Talking;
		case Mumble::Protocol::AudioContext::SHOUT:
			return Settings::Shouting;
		case Mumble::Protocol::AudioContext::WHISPER:
			return Settings::Whispering;
		case Mumble::Protocol::AudioContext::INVALID:
			return Settings::Passive;
		default:
			return Settings::Talking;
	}
}

void AudioOutputSpeech::updateTalkingStateFromAudioContext(Mumble::Protocol::audio_context_t context) {
	if (!p) {
		return;
	}

	Settings::TalkState ts = talkStateForAudioContext(context);
	if (ts != Settings::Passive && (p->bLocalMute || p->volumeMute)) {
		ts = Settings::MutedTalking;
	}

	p->setTalking(ts);
}

void AudioOutputSpeech::addFrameToBuffer(const Mumble::Protocol::AudioData &audioData) {
	if (audioData.payload.empty()) {
		return;
	}

	QMutexLocker lock(&qmJitter);

	int samples = 0;

	assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
	assert(audioData.usedCodec == m_codec);

	samples = opus_decoder_get_nb_samples(
		opusState, audioData.payload.data(),
		static_cast< int >(audioData.payload.size())); // this function return samples per channel
	samples *= 2;                                      // since we assume all input stream is stereo.

	// We can't handle frames which are not a multiple of our configured framesize.
	if (static_cast< unsigned int >(samples) % iFrameSize != 0) {
		qWarning("AudioOutputSpeech: Dropping Opus audio packet, because its sample count (%d) is not a "
				 "multiple of our frame size (%d)",
				 samples, iFrameSize);
		return;
	}

	// Copy the audio data to an AudioOutputCache instance and store that in our global chunk list
	std::size_t storageIndex = storeAudioOutputCache(audioData);

	// We cheat a bit and instead of storing the actual audio data in the jitter buffer, we store the index to
	// the created audio chunk in the buffer. Passing a length of 0 should ensure that this "pointer" will never
	// be dereferenced.

	// A call to jitter_buffer_put stores the packet in an internal array used for book-keeping.
	// The library uses jbp.data == NULL to differentiate between unused and reserved elements
	// of the book-keeping array.
	// Since a storageIndex of zero will look the same as a null pointer, we always add one to
	// ensure the library never erroneously confuses this entry with a free slot.
	JitterBufferPacket jbp;
	jbp.data      = reinterpret_cast< char * >(storageIndex) + 1;
	jbp.len       = 0;
	jbp.span      = static_cast< unsigned int >(samples);
	jbp.timestamp = static_cast< unsigned int >(iFrameSize * audioData.frameNumber);

	jitter_buffer_put(jbJitter, &jbp);

	lock.unlock();
	updateTalkingStateFromAudioContext(static_cast< Mumble::Protocol::audio_context_t >(audioData.targetOrContext));
}

bool AudioOutputSpeech::prepareSampleBuffer(unsigned int frameCount) {
	const unsigned int channels    = bStereo ? 2 : 1;
	const unsigned int sampleCount = frameCount * channels;
	// Note: all stereo support is crafted for Opus, since the other codecs are deprecated.

	const unsigned int previouslyConsumed = iLastConsume;
	for (unsigned int i = previouslyConsumed; i < iBufferFilled; ++i) {
		pfBuffer[i - previouslyConsumed] = pfBuffer[i];
	}
	iBufferFilled -= previouslyConsumed;
	if (m_outputEndKnown) {
		m_outputEndBufferOffset = previouslyConsumed >= m_outputEndBufferOffset
									? 0
									: m_outputEndBufferOffset - previouslyConsumed;
	}
	iLastConsume = sampleCount;

	// Maximum interaural delay is accounted for to prevent audio glitches.
	if (iBufferFilled >= sampleCount + INTERAURAL_DELAY) {
		const bool wasAlive = bLastAlive;
		if (m_outputEndKnown && m_outputEndBufferOffset <= sampleCount) {
			bLastAlive = false;
		}
		return wasAlive;
	}

	float *pOut   = nullptr;
	bool nextalive = bLastAlive;

	while (iBufferFilled < sampleCount + INTERAURAL_DELAY) {
		if (m_outputEndKnown) {
			// The final cleaned sample is already buffered. Add only the small
			// positional-delay safety margin; it is not part of the stream and must
			// not move the recorded end offset.
			const unsigned int requiredSamples = sampleCount + INTERAURAL_DELAY;
			resizeBuffer(requiredSamples);
			memset(pfBuffer + iBufferFilled, 0,
				   static_cast< std::size_t >(requiredSamples - iBufferFilled) * sizeof(float));
			iBufferFilled = requiredSamples;
			break;
		}

		int decodedSamples           = static_cast< int >(iFrameSize);
		bool finishStreamAfterChunk  = false;
		bool applyFadeInForChunk     = false;
		bool tickJitterForChunk      = false;
		resizeBuffer(iBufferFilled + iOutputSize + INTERAURAL_DELAY);
		// TODO: allocating memory in the audio callback will crash mumble in some cases.
		//       we need to initialize the buffer with an appropriate size when initializing
		//       this class. See #4250.

		pOut = srs ? fResamplerBuffer : pfBuffer + iBufferFilled;

		if (!bLastAlive) {
			memset(pOut, 0, iFrameSize * sizeof(float));
		} else if (m_remoteSpeechCleanupDrainSamplesRemaining > 0) {
			const unsigned int drainFrames =
				std::min(iFrameSizePerChannel, m_remoteSpeechCleanupDrainSamplesRemaining);
			decodedSamples = static_cast< int >(drainFrames * channels);
			memset(pOut, 0, static_cast< std::size_t >(decodedSamples) * sizeof(float));
			(void) applyRemoteSpeechCleanup(pOut, static_cast< unsigned int >(decodedSamples));
			m_remoteSpeechCleanupDrainSamplesRemaining -= drainFrames;
			m_remoteSpeechCleanupDrainedSamples += drainFrames;
			if (m_remoteSpeechCleanupDrainSamplesRemaining == 0) {
				m_remoteSpeechCleanupDrainCompleted = true;
				finishStreamAfterChunk              = true;
			}
		} else {
			if (p == &LoopUser::lpLoopy) {
				LoopUser::lpLoopy.fetchFrames();
			}

			int avail = 0;
			const int ts = jitter_buffer_get_pointer_timestamp(jbJitter);
			applyFadeInForChunk = ts == 0;
			tickJitterForChunk  = true;
			jitter_buffer_ctl(jbJitter, JITTER_BUFFER_GET_AVAILABLE_COUNT, &avail);

			if (p && ts == 0) {
				const int want = static_cast< int >(p->fAverageAvailable);
				if (avail < want) {
					++iMissCount;
					if (iMissCount < 20) {
						memset(pOut, 0, iFrameSize * sizeof(float));
						goto nextframe;
					}
				}
			}

			if (qlFrames.isEmpty()) {
				QMutexLocker lock(&qmJitter);
				JitterBufferPacket jbp;
				spx_int32_t startofs = 0;
				if (jitter_buffer_get(jbJitter, &jbp, static_cast< int >(iFrameSize), &startofs)
					== JITTER_BUFFER_OK) {
					std::lock_guard< std::mutex > audioChunkLock(s_audioCachesMutex);
					iMissCount = 0;

					// The "data pointer" stored in the jitter buffer is an index into s_audioCaches.
					const std::size_t index = reinterpret_cast< std::size_t >(jbp.data) - 1;
					assert(jbp.len == 0);
					assert(index < s_audioCaches.size());
					AudioOutputCache &cache = s_audioCaches[index];
					assert(cache.isValid());

					bHasTerminator = cache.isLastFrame();
					assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
					qlFrames << QByteArray(reinterpret_cast< const char * >(cache.getAudioData().data()),
										   static_cast< int >(cache.getAudioData().size()));

					if (cache.containsPositionalInformation()) {
						assert(cache.getPositionalInformation().size() == 3);
						assert(fPos.size() == 3);
						for (unsigned int i = 0; i < 3; ++i) {
							fPos[i] = cache.getPositionalInformation()[i];
						}
					} else {
						fPos[0] = fPos[1] = fPos[2] = 0.0f;
					}

					m_suggestedVolumeAdjustment = cache.getVolumeAdjustment();
					m_audioContext              = cache.getContext();
					if (p) {
						const float available = static_cast< float >(avail);
						if (available >= p->fAverageAvailable) {
							p->fAverageAvailable = available;
						} else {
							p->fAverageAvailable *= 0.99f;
						}
					}

					// We registered a destroy callback, so the returned cache entry is ours to clear.
					cache.clear();
				} else {
					jitter_buffer_update_delay(jbJitter, &jbp, nullptr);
					++iMissCount;
					if (iMissCount > 10 && !beginRemoteSpeechCleanupDrain()) {
						nextalive = false;
					}
				}
			}

			if (!qlFrames.isEmpty()) {
				const QByteArray qba = qlFrames.takeFirst();
				assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
				if (qba.isEmpty() || !(p && p->bLocalMute)) {
					decodedSamples = opus_decode_float(
						opusState, qba.isEmpty() ? nullptr : reinterpret_cast< const unsigned char * >(qba.constData()),
						static_cast< opus_int32 >(qba.size()), pOut, static_cast< int >(iAudioBufferSize / channels),
						0);
				} else {
					decodedSamples = opus_packet_get_samples_per_frame(
						reinterpret_cast< const unsigned char * >(qba.constData()), SAMPLE_RATE);
				}

				decodedSamples *= static_cast< int >(channels);
				if (decodedSamples < 0) {
					decodedSamples = static_cast< int >(iFrameSize);
					memset(pOut, 0, iFrameSize * sizeof(float));
				}
				if (decodedSamples > 0 && !(p && p->bLocalMute)) {
					applyRemoteSpeechCleanup(pOut, static_cast< unsigned int >(decodedSamples));
				}

				bool update = true;
				if (p && decodedSamples > 0) {
					float &fPowerMax = p->fPowerMax;
					float &fPowerMin = p->fPowerMin;
					float power      = 0.0f;
					for (int i = 0; i < decodedSamples; ++i) {
						power += pOut[i] * pOut[i];
					}
					power = sqrtf(power / static_cast< float >(decodedSamples));
					if (power >= fPowerMax) {
						fPowerMax = power;
					} else if (power <= fPowerMin) {
						fPowerMin = power;
					} else {
						fPowerMax = 0.99f * fPowerMax;
						fPowerMin += 0.0001f * power;
					}
					update = power < (fPowerMin + 0.01f * (fPowerMax - fPowerMin));
				}

				if (qlFrames.isEmpty() && update) {
					jitter_buffer_update_delay(jbJitter, nullptr, nullptr);
				}
				if (qlFrames.isEmpty() && bHasTerminator) {
					bHasTerminator = false;
					if (!beginRemoteSpeechCleanupDrain()) {
						m_remoteSpeechCleanupDrainCompleted = true;
						finishStreamAfterChunk              = true;
					}
				}
			} else {
				assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
				decodedSamples =
					opus_decode_float(opusState, nullptr, 0, pOut, static_cast< int >(iFrameSizePerChannel), 0);
				decodedSamples *= static_cast< int >(channels);
				if (decodedSamples < 0) {
					decodedSamples = static_cast< int >(iFrameSize);
					memset(pOut, 0, iFrameSize * sizeof(float));
				}
				// Keep causal cleanup state moving across Opus packet-loss concealment frames.
				if (decodedSamples > 0 && !(p && p->bLocalMute)) {
					applyRemoteSpeechCleanup(pOut, static_cast< unsigned int >(decodedSamples));
				}
			}

			if (finishStreamAfterChunk || !nextalive) {
				const unsigned int decodedFrames = static_cast< unsigned int >(decodedSamples) / channels;
				const unsigned int fadeFrames    = std::min(iFrameSizePerChannel, decodedFrames);
				const unsigned int fadeStart     = decodedFrames - fadeFrames;
				for (unsigned int i = 0; i < fadeFrames; ++i) {
					// A final causal-drain chunk can be shorter than the regular
					// 10 ms frame (for example DTLN's latency leaves a 336-sample
					// remainder). Map that shorter span across the complete fade
					// table so its final sample still reaches zero.
					const unsigned int fadeIndex = fullSpanFadeIndex(i, fadeFrames, iFrameSizePerChannel);
					for (unsigned int channel = 0; channel < channels; ++channel) {
						pOut[(fadeStart + i) * channels + channel] *= fFadeOut[fadeIndex];
					}
				}
			} else if (applyFadeInForChunk) {
				for (unsigned int i = 0; i < iFrameSizePerChannel; ++i) {
					for (unsigned int channel = 0; channel < channels; ++channel) {
						pOut[i * channels + channel] *= fFadeIn[i];
					}
				}
			}

			if (tickJitterForChunk) {
				for (unsigned int i = static_cast< unsigned int >(decodedSamples) / iFrameSize; i > 0; --i) {
					jitter_buffer_tick(jbJitter);
				}
			}
		}

	nextframe:
		if (p && p->bLocalMute) {
			memset(pOut, 0, static_cast< std::size_t >(decodedSamples) * sizeof(float));
		}

		spx_uint32_t inlen  = static_cast< unsigned int >(decodedSamples) / channels;
		spx_uint32_t outlen = static_cast< unsigned int >(
			ceilf(static_cast< float >(static_cast< unsigned int >(decodedSamples) / channels * iMixerFreq)
				  / static_cast< float >(iSampleRate)));
		if (srs && bLastAlive) {
			if (channels == 1) {
				speex_resampler_process_float(srs, 0, fResamplerBuffer, &inlen, pfBuffer + iBufferFilled, &outlen);
			} else {
				speex_resampler_process_interleaved_float(srs, fResamplerBuffer, &inlen, pfBuffer + iBufferFilled,
													  &outlen);
			}
		}
		iBufferFilled += outlen * channels;
		if (finishStreamAfterChunk) {
			m_outputEndKnown        = true;
			m_outputEndBufferOffset = iBufferFilled;
		}
	}

	if (m_outputEndKnown && m_outputEndBufferOffset <= sampleCount) {
		nextalive = false;
	}
	if (p) {
		if (!nextalive) {
			m_audioContext = Mumble::Protocol::AudioContext::INVALID;
		}
		updateTalkingStateFromAudioContext(m_audioContext);
	}

	const bool wasAlive = bLastAlive;
	bLastAlive          = nextalive;
	return wasAlive;
}
