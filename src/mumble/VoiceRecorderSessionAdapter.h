// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_VOICERECORDERSESSIONADAPTER_H_
#define MUMBLE_MUMBLE_VOICERECORDERSESSIONADAPTER_H_

#include "ModernRecorderController.h"
#include "VoiceRecorder.h"

namespace Mumble {

/// Production backend adapter kept separate from the frontend-neutral state
/// controller so controller tests do not require audio, server or codec state.
class VoiceRecorderSessionAdapter final : public ModernRecorderSession {
	Q_OBJECT

public:
	explicit VoiceRecorderSessionAdapter(const VoiceRecorder::Config &config, QObject *parent = nullptr);
	~VoiceRecorderSessionAdapter() override;

	void start() override;
	void stop(bool force = false) override;
	quint64 elapsedMicroseconds() const override;
	VoiceRecorderPtr recorder() const;

private:
	VoiceRecorderPtr m_recorder;
	bool m_terminalSignalDelivered = false;
};

} // namespace Mumble

#endif // MUMBLE_MUMBLE_VOICERECORDERSESSIONADAPTER_H_
