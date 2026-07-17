// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOOUTPUTSAMPLE_H_
#define MUMBLE_MUMBLE_AUDIOOUTPUTSAMPLE_H_

#include <QtCore/QFile>
#include <QtCore/QObject>
#include <sndfile.h>
#include <speex/speex_resampler.h>

#include "AudioOutputBuffer.h"

#include <cstddef>
#include <span>
#include <vector>

class SoundFile : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(SoundFile)
protected:
	SNDFILE *sfFile;
	SF_INFO siInfo;
	QFile qfFile;
	static sf_count_t vio_get_filelen(void *user_data);
	static sf_count_t vio_seek(sf_count_t offset, int whence, void *user_data);
	static sf_count_t vio_read(void *ptr, sf_count_t count, void *user_data);
	static sf_count_t vio_write(const void *ptr, sf_count_t count, void *user_data);
	static sf_count_t vio_tell(void *user_data);

public:
	SoundFile(const QString &fname);
	~SoundFile();

	int channels() const;
	int samplerate() const;
	int error() const;
	QString strError() const;
	bool isOpen() const;

	sf_count_t seek(sf_count_t frames, int whence);
	sf_count_t read(float *ptr, sf_count_t items);
};

class AudioOutputSample : public AudioOutputBuffer {
private:
	Q_OBJECT
	Q_DISABLE_COPY(AudioOutputSample)
protected:
	unsigned int iLastConsume  = 0;
	unsigned int iBufferFilled = 0;
	unsigned int iOutSampleRate = 0;
	SpeexResamplerState *srs = nullptr;

	SoundFile *sfHandle = nullptr;

	bool bLoop = false;
	bool bEof  = false;

	float m_volume = 1.0f;

	std::vector< float > m_ownedMemoryPcm;
	std::size_t m_memoryCursor = 0;
	bool m_memorySample = false;
	bool m_valid        = false;

	bool initializeMemoryPcm(std::span< const float > monoPcm, unsigned int sampleRate,
							 unsigned int outputSampleRate);
	static void secureWipe(float *samples, std::size_t sampleCount) noexcept;
signals:
	void playbackFinished();

public:
	static constexpr unsigned int memorySampleRate = 48000;
	static constexpr unsigned int maximumMemorySampleSeconds = 12;
	static constexpr std::size_t maximumMemorySampleCount =
		static_cast< std::size_t >(memorySampleRate) * maximumMemorySampleSeconds;

	static SoundFile *loadSndfile(const QString &filename);
	static QString browseForSndfile(QString defaultpath = QString());
	virtual bool prepareSampleBuffer(unsigned int frameCount) Q_DECL_OVERRIDE;
	float getVolume() const;
	bool isValid() const noexcept;
	AudioOutputSample(SoundFile *psndfile, float volume, bool repeat, unsigned int freq, unsigned int bufferSize);
	/// Creates a non-looping mono sample that owns a private copy of the supplied
	/// 48 kHz PCM. Validation, copying and optional output-rate conversion happen
	/// before the object is inserted into the real-time mixer.
	AudioOutputSample(std::span< const float > monoPcm, unsigned int sampleRate, float volume,
					  unsigned int outputSampleRate, unsigned int systemMaxBufferSize);
	~AudioOutputSample() Q_DECL_OVERRIDE;
};

#endif // AUDIOOUTPUTSAMPLE_H_
