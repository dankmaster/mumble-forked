// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPTESTOPUSOBSERVER_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPTESTOPUSOBSERVER_H_

#include <QtCore/QByteArray>

#include <cstdint>

namespace Mumble::SpeechCleanupE2E {

/// A digest of the exact buffers observed at the existing libopus call boundary.
/// This is only linked into clients built with speech-cleanup-e2e enabled.
struct OpusObservation final {
	QByteArray preOpusPcmSha256;
	QByteArray opusPacketsSha256;
	std::uint64_t packetCount = 0;
};

/// Starts observation on the calling audio thread. Until this is called, the
/// test-only wrapper is a transparent pass-through to libopus.
void beginOpusObservation();

/// Finalizes observation on the calling audio thread and restores transparent
/// pass-through behavior.
OpusObservation finishOpusObservation();

} // namespace Mumble::SpeechCleanupE2E

#endif // MUMBLE_MUMBLE_SPEECHCLEANUPTESTOPUSOBSERVER_H_
