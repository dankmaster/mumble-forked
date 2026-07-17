// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioOutputSample.h"

#include "Audio.h"
#include "Utils.h"

#include <QtCore/QDebug>
#include <QtWidgets/QFileDialog>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

SoundFile::SoundFile(const QString &fname) {
	siInfo.frames     = 0;
	siInfo.channels   = 1;
	siInfo.samplerate = 0;
	siInfo.sections   = 0;
	siInfo.seekable   = 0;
	siInfo.format     = 0;

	sfFile = nullptr;

	qfFile.setFileName(fname);

	if (qfFile.open(QIODevice::ReadOnly)) {
		static SF_VIRTUAL_IO svi = { &SoundFile::vio_get_filelen, &SoundFile::vio_seek, &SoundFile::vio_read,
									 &SoundFile::vio_write, &SoundFile::vio_tell };

		sfFile = sf_open_virtual(&svi, SFM_READ, &siInfo, this);

		if (!sfFile) {
			qWarning("AudioOutputSample: Failed to open sound-file: %s", qUtf8Printable(strError()));
		}
	}
}

SoundFile::~SoundFile() {
	if (sfFile)
		sf_close(sfFile);
}

bool SoundFile::isOpen() const {
	return sfFile && qfFile.isOpen();
}

int SoundFile::channels() const {
	return siInfo.channels;
}

int SoundFile::samplerate() const {
	return siInfo.samplerate;
}

int SoundFile::error() const {
	return sf_error(sfFile);
}

QString SoundFile::strError() const {
	return QLatin1String(sf_strerror(sfFile));
}

sf_count_t SoundFile::seek(sf_count_t frames, int whence) {
	return sf_seek(sfFile, frames, whence);
}

sf_count_t SoundFile::read(float *ptr, sf_count_t items) {
	return sf_read_float(sfFile, ptr, items);
}

sf_count_t SoundFile::vio_get_filelen(void *user_data) {
	SoundFile *sf = reinterpret_cast< SoundFile * >(user_data);

	if (!sf->qfFile.isOpen())
		return -1;

	return (sf->qfFile.size());
}

sf_count_t SoundFile::vio_seek(sf_count_t offset, int whence, void *user_data) {
	SoundFile *sf = reinterpret_cast< SoundFile * >(user_data);

	if (!sf->qfFile.isOpen())
		return -1;

	if (whence == SEEK_SET) {
		sf->qfFile.seek(offset);
	} else if (whence == SEEK_END) {
		sf->qfFile.seek(sf->qfFile.size() - offset);
	} else {
		sf->qfFile.seek(sf->qfFile.pos() + offset);
	}
	return sf->qfFile.pos();
}

sf_count_t SoundFile::vio_read(void *ptr, sf_count_t count, void *user_data) {
	SoundFile *sf = reinterpret_cast< SoundFile * >(user_data);

	if (!sf->qfFile.isOpen())
		return -1;

	return sf->qfFile.read(reinterpret_cast< char * >(ptr), count);
}

sf_count_t SoundFile::vio_write(const void *ptr, sf_count_t count, void *user_data) {
	SoundFile *sf = reinterpret_cast< SoundFile * >(user_data);

	if (!sf->qfFile.isOpen())
		return -1;

	return sf->qfFile.write(reinterpret_cast< const char * >(ptr), count);
}

sf_count_t SoundFile::vio_tell(void *user_data) {
	SoundFile *sf = reinterpret_cast< SoundFile * >(user_data);

	if (!sf->qfFile.isOpen())
		return -1;

	return sf->qfFile.pos();
}

AudioOutputSample::AudioOutputSample(SoundFile *psndfile, float volume, bool loop, unsigned int freq,
									 unsigned int systemMaxBufferSize) {
	int err;

	sfHandle       = psndfile;
	iOutSampleRate = freq;

	if (sfHandle->channels() == 1) {
		iBufferSize = systemMaxBufferSize;
		bStereo     = false;
	} else if (sfHandle->channels() == 2) {
		iBufferSize = systemMaxBufferSize * 2;
		bStereo     = true;
	} else {
		// Keep ownership of the invalid handle so the destructor can close it.
		return;
	}

	pfBuffer = new float[iBufferSize];

	/* qWarning() << "Channels: " << sfHandle->channels();
	qWarning() << "Samplerate: " << sfHandle->samplerate();
	qWarning() << "Target Sr.: " << freq;
	qWarning() << "Format: " << sfHandle->format() << endl; */

	// If the frequencies don't match initialize the resampler
	if (sfHandle->samplerate() != static_cast< int >(freq)) {
		srs = speex_resampler_init(bStereo ? 2 : 1, static_cast< unsigned int >(sfHandle->samplerate()), iOutSampleRate,
								   3, &err);
		if (err != RESAMPLER_ERR_SUCCESS) {
			qWarning() << "Initialize " << sfHandle->samplerate() << " to " << iOutSampleRate << " resampler failed!";
			if (srs) {
				speex_resampler_destroy(srs);
			}
			srs = nullptr;
			return;
		}
	} else {
		srs = nullptr;
	}

	iLastConsume = iBufferFilled = 0;
	m_volume                     = volume;
	bLoop                        = loop;
	bEof                         = false;
	m_valid                      = true;
}

AudioOutputSample::AudioOutputSample(std::span< const float > monoPcm, const unsigned int sampleRate,
									 const float volume, const unsigned int outputSampleRate,
									 const unsigned int systemMaxBufferSize)
	: iOutSampleRate(outputSampleRate), m_volume(volume), m_memorySample(true) {
	bStereo = false;
	if (!std::isfinite(volume) || volume < 0.0f
		|| systemMaxBufferSize > std::numeric_limits< unsigned int >::max() - INTERAURAL_DELAY
		|| !initializeMemoryPcm(monoPcm, sampleRate, outputSampleRate)) {
		return;
	}

	// Positional mixing may read up to INTERAURAL_DELAY samples beyond the
	// requested frame. Reserve that tail before entering the mixer so the
	// callback never has to resize this buffer.
	iBufferSize = std::max(1U, systemMaxBufferSize) + INTERAURAL_DELAY;
	pfBuffer    = new float[iBufferSize];
	std::fill_n(pfBuffer, iBufferSize, 0.0f);
	m_valid = true;
}

bool AudioOutputSample::initializeMemoryPcm(const std::span< const float > monoPcm,
										 const unsigned int sampleRate,
										 const unsigned int outputSampleRate) {
	if (sampleRate != memorySampleRate || outputSampleRate < 8000U || outputSampleRate > 192000U
		|| monoPcm.empty() || monoPcm.size() > maximumMemorySampleCount
		|| !std::all_of(monoPcm.begin(), monoPcm.end(), [](const float sample) { return std::isfinite(sample); })) {
		return false;
	}

	if (outputSampleRate == sampleRate) {
		m_ownedMemoryPcm.assign(monoPcm.begin(), monoPcm.end());
		return true;
	}

	const std::uint64_t scaledCount = static_cast< std::uint64_t >(monoPcm.size()) * outputSampleRate;
	const std::size_t outputCount = static_cast< std::size_t >(
		(scaledCount + sampleRate - 1U) / sampleRate);
	if (outputCount == 0 || outputCount > static_cast< std::size_t >(outputSampleRate) * maximumMemorySampleSeconds) {
		return false;
	}

	int error = RESAMPLER_ERR_SUCCESS;
	SpeexResamplerState *resampler = speex_resampler_init(1, sampleRate, outputSampleRate, 3, &error);
	if (!resampler || error != RESAMPLER_ERR_SUCCESS) {
		if (resampler) {
			speex_resampler_destroy(resampler);
		}
		return false;
	}

	// Remove the resampler's leading filter delay. Any filter tail still needed
	// for the exact clip duration is generated from a fixed zero block below.
	speex_resampler_skip_zeros(resampler);
	m_ownedMemoryPcm.assign(outputCount, 0.0f);

	std::size_t inputOffset  = 0;
	std::size_t outputOffset = 0;
	bool conversionOk        = true;
	while (inputOffset < monoPcm.size() && outputOffset < outputCount) {
		spx_uint32_t inputLength = static_cast< spx_uint32_t >(monoPcm.size() - inputOffset);
		spx_uint32_t outputLength = static_cast< spx_uint32_t >(outputCount - outputOffset);
		const int result = speex_resampler_process_float(resampler, 0, monoPcm.data() + inputOffset,
													&inputLength, m_ownedMemoryPcm.data() + outputOffset,
													&outputLength);
		if (result != RESAMPLER_ERR_SUCCESS || (inputLength == 0 && outputLength == 0)) {
			conversionOk = false;
			break;
		}
		inputOffset += inputLength;
		outputOffset += outputLength;
	}

	std::array< float, 256 > zeroTail = {};
	while (conversionOk && inputOffset == monoPcm.size() && outputOffset < outputCount) {
		spx_uint32_t inputLength  = static_cast< spx_uint32_t >(zeroTail.size());
		spx_uint32_t outputLength = static_cast< spx_uint32_t >(outputCount - outputOffset);
		const int result = speex_resampler_process_float(resampler, 0, zeroTail.data(), &inputLength,
													m_ownedMemoryPcm.data() + outputOffset, &outputLength);
		if (result != RESAMPLER_ERR_SUCCESS || outputLength == 0) {
			conversionOk = false;
			break;
		}
		outputOffset += outputLength;
	}

	speex_resampler_destroy(resampler);
	if (!conversionOk || inputOffset != monoPcm.size()) {
		secureWipe(m_ownedMemoryPcm.data(), m_ownedMemoryPcm.size());
		m_ownedMemoryPcm.clear();
		return false;
	}
	return true;
}

void AudioOutputSample::secureWipe(float *samples, const std::size_t sampleCount) noexcept {
	volatile float *cursor = samples;
	for (std::size_t index = 0; cursor && index < sampleCount; ++index) {
		cursor[index] = 0.0f;
	}
}

float AudioOutputSample::getVolume() const {
	return m_volume;
}

bool AudioOutputSample::isValid() const noexcept {
	return m_valid;
}

AudioOutputSample::~AudioOutputSample() {
	if (m_memorySample) {
		secureWipe(m_ownedMemoryPcm.data(), m_ownedMemoryPcm.size());
		secureWipe(pfBuffer, iBufferSize);
	}
	if (srs)
		speex_resampler_destroy(srs);

	delete sfHandle;
	sfHandle = nullptr;
}

SoundFile *AudioOutputSample::loadSndfile(const QString &filename) {
	SoundFile *sf;

	// Create the filehandle and do a quick check if everything is ok
	sf = new SoundFile(filename);

	if (!sf->isOpen()) {
		qWarning() << "File " << filename << " failed to open";
		delete sf;
		return nullptr;
	}

	if (sf->error() != SF_ERR_NO_ERROR) {
		qWarning() << "File " << filename << " couldn't be loaded: " << sf->strError();
		delete sf;
		return nullptr;
	}

	if (sf->channels() <= 0 || sf->channels() > 2) {
		qWarning() << "File " << filename << " contains " << sf->channels() << " Channels, only 1 or 2 are supported.";
		delete sf;
		return nullptr;
	}
	return sf;
}

QString AudioOutputSample::browseForSndfile(QString defaultpath) {
	QString file = QFileDialog::getOpenFileName(nullptr, tr("Choose sound file"), defaultpath,
												QLatin1String("*.wav *.ogg *.ogv *.oga *.flac *.aiff"));
	if (!file.isEmpty()) {
		SoundFile *sf = AudioOutputSample::loadSndfile(file);
		if (!sf) {
			qWarning().noquote() << tr("The file '%1' cannot be used by Mumble. Please select a file with a compatible "
										 "format and encoding.")
									 .arg(file);
			return QString();
		}
		delete sf;
	}
	return file;
}

bool AudioOutputSample::prepareSampleBuffer(unsigned int frameCount) {
	if (!m_valid || frameCount == 0) {
		return false;
	}

	if (m_memorySample) {
		if (m_memoryCursor >= m_ownedMemoryPcm.size()
			|| frameCount > iBufferSize || INTERAURAL_DELAY > iBufferSize - frameCount) {
			return false;
		}

		const std::size_t requiredSamples = static_cast< std::size_t >(frameCount) + INTERAURAL_DELAY;
		std::fill_n(pfBuffer, requiredSamples, 0.0f);
		const std::size_t remaining = m_ownedMemoryPcm.size() - m_memoryCursor;
		const std::size_t toCopy    = std::min< std::size_t >(frameCount, remaining);
		std::copy_n(m_ownedMemoryPcm.data() + m_memoryCursor, toCopy, pfBuffer);
		m_memoryCursor += toCopy;
		bEof = m_memoryCursor == m_ownedMemoryPcm.size();

		// Deliberately return one final, zero-padded frame. The following mixer
		// callback observes EOF and removes the sample. No signal/event or memory
		// allocation is performed from this real-time path.
		return true;
	}

	unsigned int channels    = bStereo ? 2 : 1;
	unsigned int sampleCount = frameCount * channels;
	// Forward the buffer
	for (unsigned int i = iLastConsume; i < iBufferFilled; ++i)
		pfBuffer[i - iLastConsume] = pfBuffer[i];
	iBufferFilled -= iLastConsume;
	iLastConsume = sampleCount;

	// Check if we can satisfy request with current buffer
	// Maximum interaural delay is accounted for to prevent audio glitches
	if (static_cast< float >(iBufferFilled) >= static_cast< float >(sampleCount) + INTERAURAL_DELAY)
		return true;

	// Calculate the required buffersize to hold the results
	unsigned int iInputFrames = static_cast< unsigned int >(
		ceilf(static_cast< float >(frameCount * static_cast< unsigned int >(sfHandle->samplerate()))
			  / static_cast< float >(iOutSampleRate)));
	unsigned int iInputSamples = iInputFrames * channels;

	static std::vector< float > fOut;
	fOut.resize(iInputSamples);

	bool eof = false;
	sf_count_t read;
	do {
		resizeBuffer(iBufferFilled + sampleCount + INTERAURAL_DELAY);

		// If we need to resample, write to the buffer on stack
		float *pOut = (srs) ? fOut.data() : pfBuffer + iBufferFilled;

		// Try to read all samples needed to satisfy this request
		if ((read = sfHandle->read(pOut, iInputSamples)) < iInputSamples) {
			if (sfHandle->error() != SF_ERR_NO_ERROR || !bLoop) {
				// We reached the eof or encountered an error, stuff with zeroes
				memset(pOut, 0, sizeof(float) * static_cast< std::size_t >(iInputSamples - read));
				read = iInputSamples;
				eof  = true;
			} else {
				sfHandle->seek(SEEK_SET, 0);
			}
		}

		spx_uint32_t inlen  = static_cast< unsigned int >(read) / channels;
		spx_uint32_t outlen = frameCount;
		if (srs) {
			// If necessary resample
			if (!bStereo) {
				speex_resampler_process_float(srs, 0, pOut, &inlen, pfBuffer + iBufferFilled, &outlen);
			} else {
				speex_resampler_process_interleaved_float(srs, pOut, &inlen, pfBuffer + iBufferFilled, &outlen);
			}
		}

		iBufferFilled += outlen * channels;
	} while (iBufferFilled < sampleCount + INTERAURAL_DELAY);

	if (eof && !bEof) {
		emit playbackFinished();
		bEof = true;
	}

	return !eof;
}
