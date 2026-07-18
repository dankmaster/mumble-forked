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
		unsigned int causalDrainSamples = 0;
		bool draining                 = false;
		bool terminalFlush            = false;
		bool terminator               = false;
	};

	/// Begins a causal processor drain and optionally appends callback-only
	/// frames that flush downstream state. Terminal flush frames deliberately do
	/// not contribute to requestedSamples()/drainedSamples(): they recover audio
	/// already produced by the cleanup processor, rather than adding cleanup
	/// latency.
	void begin(unsigned int latencySamples, unsigned int terminalFlushFrames = 0) noexcept {
		m_requestedSamples            = latencySamples;
		m_remainingSamples            = latencySamples;
		m_remainingTerminalFlushFrames = terminalFlushFrames;
	}
	void cancel() noexcept {
		m_requestedSamples             = 0;
		m_remainingSamples             = 0;
		m_remainingTerminalFlushFrames = 0;
	}

	bool active() const noexcept { return m_remainingSamples > 0 || m_remainingTerminalFlushFrames > 0; }
	unsigned int requestedSamples() const noexcept { return m_requestedSamples; }
	unsigned int remainingSamples() const noexcept { return m_remainingSamples; }
	unsigned int remainingTerminalFlushFrames() const noexcept { return m_remainingTerminalFlushFrames; }
	unsigned int drainedSamples() const noexcept { return m_requestedSamples - m_remainingSamples; }

	Frame takeFrame(unsigned int frameSize) noexcept {
		if (!active() || frameSize == 0) {
			return {};
		}

		Frame frame;
		frame.draining = true;
		if (m_remainingSamples > 0) {
			frame.zeroInputSamples = std::min(m_remainingSamples, frameSize);
			frame.causalDrainSamples = frame.zeroInputSamples;
			m_remainingSamples -= frame.causalDrainSamples;
		} else {
			// A terminal flush is still a real, full zero-input callback through
			// the cleanup processor. It advances no causal cleanup latency; its
			// only purpose is to release output buffered by a downstream stage.
			frame.zeroInputSamples = frameSize;
			frame.terminalFlush    = true;
			--m_remainingTerminalFlushFrames;
		}
		frame.terminator = !active();
		return frame;
	}

private:
	unsigned int m_requestedSamples = 0;
	unsigned int m_remainingSamples = 0;
	unsigned int m_remainingTerminalFlushFrames = 0;
};

} // namespace Mumble::SpeechCleanup

#endif
