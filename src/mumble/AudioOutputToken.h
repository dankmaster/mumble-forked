// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOOUTPUTTOKEN_H_
#define MUMBLE_MUMBLE_AUDIOOUTPUTTOKEN_H_

#include "AudioOutput.h"

#include <QObject>

#include <cassert>
#include <cstdint>

class AudioOutputBuffer;

class AudioOutputToken {
public:
	AudioOutputToken() = default;
	AudioOutputToken(AudioOutputBuffer *buffer, const std::uint64_t tokenId)
		: m_buffer(buffer), m_tokenId(tokenId) {}

	~AudioOutputToken() = default;

	inline bool operator==(const AudioOutputToken &rhs) const {
		return m_buffer == rhs.m_buffer && m_tokenId == rhs.m_tokenId;
	}
	inline bool operator!=(const AudioOutputToken &rhs) const { return !(*this == rhs); }

	operator bool() const { return m_buffer; }

	template< typename UnderlyingType, typename SignalFunc, typename SlotObject, typename SlotFunc >
	void connect(SignalFunc signalFunc, SlotObject &slotObject, SlotFunc slotFunc) {
		assert(dynamic_cast< UnderlyingType * >(m_buffer));
		QObject::connect(dynamic_cast< UnderlyingType * >(m_buffer), signalFunc, &slotObject, slotFunc);
	}

private:
	AudioOutputBuffer *m_buffer = nullptr;
	std::uint64_t m_tokenId     = 0;

	friend class AudioOutput;
};

#endif // AUDIOOUTPUTTOKEN_H_
