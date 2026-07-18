// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernRecorderRuntimeAdapter.h"

#include "AudioOutput.h"
#include "Global.h"
#include "ServerHandler.h"
#include "Settings.h"
#include "Version.h"
#include "VoiceRecorder.h"
#include "VoiceRecorderSessionAdapter.h"

namespace Mumble {

namespace {
	ModernRecorderRuntimeResult failure(const QString &code, const QString &message) {
		return ModernRecorderRuntimeResult::failure(code, message);
	}

	VoiceRecorderSessionAdapter *voiceSession(ModernRecorderSession *session) {
		return qobject_cast< VoiceRecorderSessionAdapter * >(session);
	}
} // namespace

ModernRecorderRuntimeAdapter::ModernRecorderRuntimeAdapter(QObject *parent) : QObject(parent) {}

bool ModernRecorderRuntimeAdapter::transportSupported() const {
	const AudioOutputPtr audioOutput = Global::get().ao;
	return audioOutput && audioOutput->supportsTransportRecording();
}

QVariantList ModernRecorderRuntimeAdapter::formatOptions() const {
	QVariantList options;
	options.reserve(VoiceRecorderFormat::kEnd);
	for (int format = 0; format < VoiceRecorderFormat::kEnd; ++format) {
		options.push_back(QVariantMap {
			{ QStringLiteral("label"), VoiceRecorderFormat::getFormatDescription(
				static_cast< VoiceRecorderFormat::Format >(format)) },
			{ QStringLiteral("value"), format },
			{ QStringLiteral("enabled"), true }
		});
	}
	return options;
}

QString ModernRecorderRuntimeAdapter::defaultExtension(const int format) const {
	if (format < 0 || format >= VoiceRecorderFormat::kEnd) return {};
	return VoiceRecorderFormat::getFormatDefaultExtension(static_cast< VoiceRecorderFormat::Format >(format));
}

ModernRecorderRuntimeResult ModernRecorderRuntimeAdapter::preflight(
		const ModernRecorderConfiguration &configuration) const {
	const ServerHandlerPtr handler = Global::get().serverHandlerSnapshot();
	if (!Global::get().uiSession || !handler || !handler->isConnected()) {
		return failure(QStringLiteral("not_connected"), tr("Connect to a server before starting a recording."));
	}
	if (handler->protocolVersion() < Version::fromComponents(1, 2, 3)) {
		return failure(QStringLiteral("server_too_old"),
			tr("This server is too old to announce recordings safely."));
	}
	if (!Global::get().recordingAllowed) {
		return failure(QStringLiteral("recording_not_allowed"),
			tr("Recording is not allowed in the current server session."));
	}
	if (handler->voiceRecorder()) {
		return failure(QStringLiteral("recorder_already_active"),
			tr("A recorder is already active for this server."));
	}
	const AudioOutputPtr audioOutput = Global::get().ao;
	if (!audioOutput) {
		return failure(QStringLiteral("audio_output_unavailable"), tr("Audio output is not available."));
	}
	if (audioOutput->getMixerFreq() == 0) {
		return failure(QStringLiteral("invalid_sample_rate"),
			tr("Audio output has a 0 Hz sample rate."));
	}
	if (configuration.format < 0 || configuration.format >= VoiceRecorderFormat::kEnd) {
		return failure(QStringLiteral("invalid_format"), tr("Select an available recording format."));
	}
	if (configuration.transportEnabled && !audioOutput->supportsTransportRecording()) {
		return failure(QStringLiteral("transport_unavailable"),
			tr("The current audio output does not support transport recording."));
	}
	return {};
}

ModernRecorderSession *ModernRecorderRuntimeAdapter::createSession(
		const ModernRecorderConfiguration &configuration, QObject *parent, ModernRecorderRuntimeResult *result) {
	ModernRecorderRuntimeResult checked = preflight(configuration);
	if (!checked.success) {
		if (result) *result = checked;
		return nullptr;
	}

	const ServerHandlerPtr handler = Global::get().serverHandlerSnapshot();
	const AudioOutputPtr audioOutput = Global::get().ao;
	if (!handler || !audioOutput) {
		checked = failure(QStringLiteral("runtime_changed"),
			tr("The connection or audio output changed while starting the recorder."));
		if (result) *result = checked;
		return nullptr;
	}

	VoiceRecorder::Config voiceConfig;
	voiceConfig.sampleRate = static_cast< int >(audioOutput->getMixerFreq());
	voiceConfig.fileName = configuration.resolvedOutputPath;
	voiceConfig.mixDownMode = configuration.mixDown;
	voiceConfig.transportEnable = configuration.transportEnabled;
	voiceConfig.recordingFormat = static_cast< VoiceRecorderFormat::Format >(configuration.format);
	auto *session = new VoiceRecorderSessionAdapter(voiceConfig, parent);
	m_sessionHandlers.insert(session, handler);
	connect(session, &QObject::destroyed, this, [this, session]() { m_sessionHandlers.remove(session); });
	if (result) *result = {};
	return session;
}

ModernRecorderRuntimeResult ModernRecorderRuntimeAdapter::validateSession(ModernRecorderSession *session) const {
	if (!voiceSession(session) || !m_sessionHandlers.contains(session)) {
		return failure(QStringLiteral("unknown_session"), tr("The recording session is no longer available."));
	}
	return {};
}

std::shared_ptr< ServerHandler > ModernRecorderRuntimeAdapter::retainedHandler(
		ModernRecorderSession *session) const {
	return m_sessionHandlers.value(session);
}

ModernRecorderRuntimeResult ModernRecorderRuntimeAdapter::attach(ModernRecorderSession *session) {
	ModernRecorderRuntimeResult result = validateSession(session);
	if (!result.success) return result;
	VoiceRecorderSessionAdapter *adapter = voiceSession(session);
	const ServerHandlerPtr handler = retainedHandler(session);
	const ServerHandlerPtr current = Global::get().serverHandlerSnapshot();
	if (!handler || current != handler || !Global::get().uiSession || !handler->isConnected()) {
		return failure(QStringLiteral("connection_changed"),
			tr("The server connection changed while the recording was paused."));
	}
	if (!Global::get().recordingAllowed) {
		return failure(QStringLiteral("recording_not_allowed"),
			tr("Recording is no longer allowed in the current server session."));
	}
	const VoiceRecorderPtr existing = handler->voiceRecorder();
	if (existing && existing.get() != adapter->recorder().get()) {
		return failure(QStringLiteral("recorder_replaced"),
			tr("Another recorder became active for this server."));
	}
	handler->setVoiceRecorder(adapter->recorder());
	return {};
}

ModernRecorderRuntimeResult ModernRecorderRuntimeAdapter::detach(ModernRecorderSession *session) {
	ModernRecorderRuntimeResult result = validateSession(session);
	if (!result.success) return result;
	VoiceRecorderSessionAdapter *adapter = voiceSession(session);
	const ServerHandlerPtr handler = retainedHandler(session);
	if (!handler) return {};
	const VoiceRecorderPtr existing = handler->voiceRecorder();
	if (!existing) return {};
	if (existing.get() != adapter->recorder().get()) {
		return failure(QStringLiteral("recorder_replaced"),
			tr("The server is using a different recorder."));
	}
	return handler->clearVoiceRecorder(existing.get()) ? ModernRecorderRuntimeResult {}
		: failure(QStringLiteral("detach_failed"), tr("The recorder could not be detached from the server."));
}

void ModernRecorderRuntimeAdapter::announceRecordingState(ModernRecorderSession *session, const bool recording) {
	const ServerHandlerPtr handler = retainedHandler(session);
	if (handler) handler->announceRecordingState(recording);
}

void ModernRecorderRuntimeAdapter::persistConfiguration(const ModernRecorderConfiguration &configuration) {
	Global::get().s.qsRecordingPath = configuration.outputDirectory;
	Global::get().s.qsRecordingFile = configuration.fileName;
	Global::get().s.iRecordingFormat = configuration.format;
	Global::get().s.rmRecordingMode = static_cast< Settings::RecordingMode >(configuration.mode);
	Global::get().s.save();
}

} // namespace Mumble
