// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_

#include "SpeechCleanup.h"

#include <QString>

#include <memory>

class SpeechCleanupProcessor {
public:
	virtual ~SpeechCleanupProcessor() = default;

	virtual bool isReady() const = 0;
	virtual void reset()         = 0;
	/// Returns the fixed causal delay, in 48 kHz mono samples, introduced by
	/// this processor. Both the cleaned signal and the dry signal used by
	/// processInPlace() are placed on this timeline.
	virtual unsigned int latencySamples() const {
		return 0;
	}
	virtual void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) = 0;
	virtual QString activeModelId() const {
		return {};
	}
	virtual QString activeModelPath() const {
		return {};
	}
	virtual bool usedFallback() const {
		return false;
	}
};

std::unique_ptr< SpeechCleanupProcessor > createSpeechCleanupProcessor(
	const Mumble::SpeechCleanup::Selection &selection);

/// Returns whether the selected noise-cancellation mode consumes a neural
/// speech-cleanup processor. Off and Speex must remain allocation-free even if
/// a neural backend/model remains selected in settings.
bool noiseCancelUsesSpeechCleanup(Settings::NoiseCancel mode) noexcept;

/// Keeps the normalized selection available for diagnostics while lazily
/// owning the heavyweight processor only for RNN/Both modes. Switching to Off
/// or Speex releases an existing processor immediately.
void reconcileSpeechCleanupProcessor(
	Settings::NoiseCancel mode, const Mumble::SpeechCleanup::Selection &requestedSelection,
	Mumble::SpeechCleanup::Selection &activeSelection,
	std::unique_ptr< SpeechCleanupProcessor > &processor);

#endif // MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
