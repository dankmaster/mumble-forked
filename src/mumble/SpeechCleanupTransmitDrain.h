// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPTRANSMITDRAIN_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPTRANSMITDRAIN_H_

#include <algorithm>

namespace Mumble::SpeechCleanup {

/// Tracks the exact amount of zero input needed to flush a causal cleanup
/// processor after microphone transmission has stopped.
class TransmitDrain {
public:
	struct Frame {
		unsigned int zeroInputSamples = 0;
		bool draining                 = false;
		bool terminator               = false;
	};

	void begin(unsigned int latencySamples) noexcept { m_remainingSamples = latencySamples; }
	void cancel() noexcept { m_remainingSamples = 0; }

	bool active() const noexcept { return m_remainingSamples > 0; }
	unsigned int remainingSamples() const noexcept { return m_remainingSamples; }

	Frame takeFrame(unsigned int frameSize) noexcept {
		if (!active() || frameSize == 0) {
			return {};
		}

		Frame frame;
		frame.draining         = true;
		frame.zeroInputSamples = std::min(m_remainingSamples, frameSize);
		m_remainingSamples -= frame.zeroInputSamples;
		frame.terminator = !active();
		return frame;
	}

private:
	unsigned int m_remainingSamples = 0;
};

} // namespace Mumble::SpeechCleanup

#endif
