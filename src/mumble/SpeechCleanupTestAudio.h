// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPTESTAUDIO_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPTESTAUDIO_H_

#include "AudioInput.h"
#include "AudioOutput.h"

#include <QtCore/QString>

#include <sndfile.h>

#include <cstdint>
#include <vector>

class ClientUser;

/// Deterministic, developer-only audio backend for end-to-end speech-cleanup tests.
///
/// The backend is only compiled when the speech-cleanup-e2e CMake option is enabled,
/// and its registrars are only installed when an explicit runtime enable flag and
/// non-empty run token are present. It is not intended for interactive use.
class SpeechCleanupTestAudioInput final : public AudioInput {
public:
	SpeechCleanupTestAudioInput();
	~SpeechCleanupTestAudioInput() override;

	void run() override;

private:
	void writeDone(bool ok, const QString &errorMessage, std::uint64_t sourceFrames,
				   std::uint64_t submittedFrames) const;
	bool m_terminatorSubmitted = false;
	unsigned int m_drainedCleanupSamples = 0;
};

class SpeechCleanupTestAudioOutput final : public AudioOutput {
public:
	SpeechCleanupTestAudioOutput();
	~SpeechCleanupTestAudioOutput() override;

	void run() override;

private:
	bool openCapture(QString *errorMessage);
	void closeCapture();
	void captureSource(float *outputPCM, unsigned int sampleCount, unsigned int channelCount,
					   unsigned int sampleRate, bool isSpeech, const ClientUser *user);
	void writeDone(bool ok, const QString &errorMessage, bool stopGateObserved) const;

	SNDFILE *m_captureFile = nullptr;
	QString m_capturePath;
	QString m_captureSender;
	std::vector< float > m_monoBuffer;
	std::uint64_t m_capturedFrames = 0;
	std::uint64_t m_captureCallbacks = 0;
	std::uint64_t m_captureFramesToSkip = 0;
	std::uint64_t m_discardedPreRollFrames = 0;
};

#endif // MUMBLE_MUMBLE_SPEECHCLEANUPTESTAUDIO_H_
