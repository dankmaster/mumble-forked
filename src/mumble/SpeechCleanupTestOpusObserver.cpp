// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SpeechCleanupTestOpusObserver.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QCryptographicHash>
#include <QtCore/QtEndian>

#include <opus.h>

#include <memory>
#include <vector>

namespace {

struct ObservationState final {
	QCryptographicHash preOpusPcm{ QCryptographicHash::Sha256 };
	QCryptographicHash opusPackets{ QCryptographicHash::Sha256 };
	std::uint64_t packetCount = 0;
};

// AudioInput invokes libopus synchronously from its backend thread. Keeping the
// observer thread-local prevents an enabled sender from observing unrelated
// encoders in the same process and adds no locking to the audio path.
thread_local std::unique_ptr< ObservationState > g_observation;

} // namespace

namespace Mumble::SpeechCleanupE2E {

void beginOpusObservation() {
	g_observation = std::make_unique< ObservationState >();
}

OpusObservation finishOpusObservation() {
	OpusObservation result;
	if (!g_observation) {
		return result;
	}

	result.preOpusPcmSha256  = g_observation->preOpusPcm.result();
	result.opusPacketsSha256 = g_observation->opusPackets.result();
	result.packetCount       = g_observation->packetCount;
	g_observation.reset();
	return result;
}

} // namespace Mumble::SpeechCleanupE2E

/// AudioInput.cpp is compiled with opus_encode macro-mapped to this function
/// only in the explicit speech-cleanup-e2e build. The protected encoder source
/// and all production builds remain unchanged. Each call first invokes the real
/// libopus function, then hashes its actual input and successful output.
extern "C" opus_int32 mumble_speech_cleanup_e2e_opus_encode(OpusEncoder *encoder, const opus_int16 *pcm, int frameSize,
															unsigned char *data, opus_int32 maximumDataBytes) {
	const opus_int32 encodedBytes = opus_encode(encoder, pcm, frameSize, data, maximumDataBytes);
	if (!g_observation || encodedBytes <= 0) {
		return encodedBytes;
	}

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
	g_observation->preOpusPcm.addData(
		QByteArrayView(reinterpret_cast< const char * >(pcm),
					   static_cast< qsizetype >(frameSize) * static_cast< qsizetype >(sizeof(opus_int16))));
#else
	std::vector< quint16 > canonicalPcm(static_cast< std::size_t >(frameSize));
	for (int index = 0; index < frameSize; ++index) {
		canonicalPcm[static_cast< std::size_t >(index)] = qToLittleEndian(static_cast< quint16 >(pcm[index]));
	}
	g_observation->preOpusPcm.addData(QByteArrayView(reinterpret_cast< const char * >(canonicalPcm.data()),
													 static_cast< qsizetype >(canonicalPcm.size() * sizeof(quint16))));
#endif
	const quint32 littleEndianLength = qToLittleEndian(static_cast< quint32 >(encodedBytes));
	g_observation->opusPackets.addData(QByteArrayView(reinterpret_cast< const char * >(&littleEndianLength),
													  static_cast< qsizetype >(sizeof(littleEndianLength))));
	g_observation->opusPackets.addData(
		QByteArrayView(reinterpret_cast< const char * >(data), static_cast< qsizetype >(encodedBytes)));
	++g_observation->packetCount;
	return encodedBytes;
}
