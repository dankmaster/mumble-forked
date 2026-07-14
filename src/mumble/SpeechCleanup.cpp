// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SpeechCleanup.h"

namespace Mumble::SpeechCleanup {

bool isBackendAvailable(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
#ifdef USE_RNNOISE
			return true;
#else
			return false;
#endif
		case Settings::DTLNBackend:
#ifdef USE_DTLN
			return true;
#else
			return false;
#endif
		case Settings::DeepFilterNetBackend:
#ifdef USE_DEEPFILTERNET
			return true;
#else
			return false;
#endif
	}

	return false;
}

QString unavailableReason(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
#ifdef USE_RNNOISE
			return QString();
#else
			return QObject::tr("RNNoise support is not compiled into this build.");
#endif
		case Settings::DTLNBackend:
#ifdef USE_DTLN
			return QString();
#else
			return QObject::tr("DTLN support is not compiled into this build.");
#endif
		case Settings::DeepFilterNetBackend:
#ifdef USE_DEEPFILTERNET
			return QString();
#else
			return QObject::tr("DeepFilterNet support is not compiled into this build.");
#endif
	}

	return QObject::tr("This speech cleanup backend is not available.");
}

bool hasAnyAvailableBackend() {
	for (Settings::SpeechCleanupBackend backend : supportedBackends) {
		if (isBackendAvailable(backend)) {
			return true;
		}
	}

	return false;
}

Settings::SpeechCleanupBackend fallbackBackend() {
	for (Settings::SpeechCleanupBackend backend : supportedBackends) {
		if (isBackendAvailable(backend)) {
			return backend;
		}
	}

	return Settings::RNNoiseBackend;
}

} // namespace Mumble::SpeechCleanup
