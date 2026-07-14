// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SpeechCleanupTestAudio.h"

#include "ClientUser.h"
#include "AudioOutputSpeech.h"
#include "Global.h"
#include "ServerHandler.h"
#include "SpeechCleanupProcessor.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

namespace {

constexpr auto TEST_FRAME_DURATION = std::chrono::milliseconds(10);
constexpr unsigned int TEST_SAMPLE_RATE = SAMPLE_RATE;
constexpr unsigned int TEST_FRAME_SIZE  = TEST_SAMPLE_RATE / 100;

const QString ENV_ENABLE          = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_ENABLE");
const QString ENV_TOKEN           = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_TOKEN");
const QString ENV_INPUT_WAV       = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_WAV");
const QString ENV_START_GATE      = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_START_GATE");
const QString ENV_INPUT_DONE      = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_DONE_PATH");
const QString ENV_INPUT_TAIL      = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_TAIL_FRAMES");
const QString ENV_INPUT_PREROLL   = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_INPUT_PREROLL_FRAMES");
const QString ENV_CAPTURE_WAV     = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_CAPTURE_WAV");
const QString ENV_CAPTURE_SENDER  = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_CAPTURE_SENDER");
const QString ENV_STOP_GATE       = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_STOP_GATE");
const QString ENV_CAPTURE_DONE    = QStringLiteral("MUMBLE_SPEECH_CLEANUP_E2E_DONE_PATH");

QString environmentValue(const QString &name) {
	return qEnvironmentVariable(name.toUtf8().constData()).trimmed();
}

bool runtimeEnabled() {
	return environmentValue(ENV_ENABLE) == QLatin1String("1") && !environmentValue(ENV_TOKEN).isEmpty();
}

bool gateMatchesRunToken(const QString &path) {
	if (path.isEmpty()) {
		return false;
	}

	QFile gate(path);
	if (!gate.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	return QString::fromUtf8(gate.readAll()).trimmed() == environmentValue(ENV_TOKEN);
}

bool writeJsonAtomically(const QString &path, const QJsonObject &object, QString *errorMessage = nullptr) {
	if (path.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("No completion path was configured");
		}
		return false;
	}

	const QFileInfo fileInfo(path);
	if (!QDir().mkpath(fileInfo.absolutePath())) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not create completion directory '%1'").arg(fileInfo.absolutePath());
		}
		return false;
	}

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (errorMessage) {
			*errorMessage = file.errorString();
		}
		return false;
	}

	const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Indented);
	if (file.write(encoded) != encoded.size() || !file.commit()) {
		if (errorMessage) {
			*errorMessage = file.errorString();
		}
		return false;
	}

	return true;
}

SNDFILE *openSoundFile(const QString &path, int mode, SF_INFO *info) {
#ifdef Q_OS_WIN
	return sf_wchar_open(path.toStdWString().c_str(), mode, info);
#else
	return sf_open(path.toUtf8().constData(), mode, info);
#endif
}

void paceRealtime(std::chrono::steady_clock::time_point &nextDeadline) {
	nextDeadline += TEST_FRAME_DURATION;
	const auto now = std::chrono::steady_clock::now();
	if (now < nextDeadline) {
		std::this_thread::sleep_until(nextDeadline);
	} else if (now - nextDeadline > std::chrono::milliseconds(50)) {
		// Do not submit a large burst after a debugger pause or heavily delayed frame.
		nextDeadline = now;
	}
}

int configuredTailFrames() {
	bool ok       = false;
	const int raw = environmentValue(ENV_INPUT_TAIL).toInt(&ok);
	return ok ? std::clamp(raw, 0, 500) : 0;
}

int configuredPreRollFrames() {
	bool ok       = false;
	const int raw = environmentValue(ENV_INPUT_PREROLL).toInt(&ok);
	return ok ? std::clamp(raw, 0, 500) : 0;
}

QString noiseCancelModeName(Settings::NoiseCancel mode) {
	switch (mode) {
		case Settings::NoiseCancelOff:
			return QStringLiteral("Off");
		case Settings::NoiseCancelSpeex:
			return QStringLiteral("Speex");
		case Settings::NoiseCancelRNN:
			return QStringLiteral("RNN");
		case Settings::NoiseCancelBoth:
			return QStringLiteral("Speex&RNN");
	}
	return QStringLiteral("Unknown");
}

QString remoteSpeechCleanupPresetName(Settings::RemoteSpeechCleanupPreset preset) {
	switch (preset) {
		case Settings::Light:
			return QStringLiteral("Light");
		case Settings::Normal:
			return QStringLiteral("Normal");
		case Settings::Aggressive:
			return QStringLiteral("Aggressive");
	}
	return QStringLiteral("Unknown");
}

class SpeechCleanupTestAudioInputRegistrar final : public AudioInputRegistrar {
public:
	SpeechCleanupTestAudioInputRegistrar() : AudioInputRegistrar(QStringLiteral("SpeechCleanupTest"), -1000) {}

	AudioInput *create() override { return runtimeEnabled() ? new SpeechCleanupTestAudioInput() : nullptr; }
	const QVariant getDeviceChoice() override { return {}; }
	const QList< audioDevice > getDeviceChoices() override {
		return { audioDevice(QStringLiteral("Deterministic WAV input"), QVariant()) };
	}
	void setDeviceChoice(const QVariant &, Settings &) override {}
	bool canEcho(EchoCancelOptionID, const QString &) const override { return false; }
	bool isMicrophoneAccessDeniedByOS() override { return false; }
};

class SpeechCleanupTestAudioOutputRegistrar final : public AudioOutputRegistrar {
public:
	SpeechCleanupTestAudioOutputRegistrar() : AudioOutputRegistrar(QStringLiteral("SpeechCleanupTest"), -1000) {}

	AudioOutput *create() override { return runtimeEnabled() ? new SpeechCleanupTestAudioOutput() : nullptr; }
	const QVariant getDeviceChoice() override { return {}; }
	const QList< audioDevice > getDeviceChoices() override {
		return { audioDevice(QStringLiteral("Deterministic capture sink"), QVariant()) };
	}
	void setDeviceChoice(const QVariant &, Settings &) override {}
};

class SpeechCleanupTestAudioInit final : public DeferInit {
public:
	void initialize() override {
		if (!runtimeEnabled()) {
			return;
		}

		m_inputRegistrar  = std::make_unique< SpeechCleanupTestAudioInputRegistrar >();
		m_outputRegistrar = std::make_unique< SpeechCleanupTestAudioOutputRegistrar >();
		qInfo("SpeechCleanupTestAudio: deterministic E2E backend enabled for this process");
	}

	void destroy() override {
		m_outputRegistrar.reset();
		m_inputRegistrar.reset();
	}

private:
	std::unique_ptr< SpeechCleanupTestAudioInputRegistrar > m_inputRegistrar;
	std::unique_ptr< SpeechCleanupTestAudioOutputRegistrar > m_outputRegistrar;
};

SpeechCleanupTestAudioInit g_speechCleanupTestAudioInit;

} // namespace

SpeechCleanupTestAudioInput::SpeechCleanupTestAudioInput() = default;

SpeechCleanupTestAudioInput::~SpeechCleanupTestAudioInput() {
	bRunning = false;
	wait();
}

void SpeechCleanupTestAudioInput::writeDone(bool ok, const QString &errorMessage, std::uint64_t sourceFrames,
											 std::uint64_t submittedFrames) const {
	const SpeechCleanupProcessor *processor = speechCleanupProcessorForDiagnostics();
	const Mumble::SpeechCleanup::Selection &selection = speechCleanupSelectionForDiagnostics();
	QJsonObject result {
		{ QStringLiteral("ok"), ok },
		{ QStringLiteral("role"), QStringLiteral("input") },
		{ QStringLiteral("completed_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
		{ QStringLiteral("input_wav"), environmentValue(ENV_INPUT_WAV) },
		{ QStringLiteral("sample_rate"), static_cast< int >(TEST_SAMPLE_RATE) },
		{ QStringLiteral("source_frames"), QString::number(sourceFrames) },
		{ QStringLiteral("submitted_frames"), QString::number(submittedFrames) },
		{ QStringLiteral("tail_frames"), configuredTailFrames() },
		{ QStringLiteral("pre_roll_frames"), configuredPreRollFrames() },
		{ QStringLiteral("terminator_submitted"), m_terminatorSubmitted },
		{ QStringLiteral("drained_cleanup_samples"), static_cast< int >(m_drainedCleanupSamples) },
		{ QStringLiteral("effective_cleanup_mode"), noiseCancelModeName(noiseCancel) },
		{ QStringLiteral("requested_backend"),
		  Mumble::SpeechCleanup::backendDisplayName(selection.backend) },
		{ QStringLiteral("requested_model_id"), selection.modelId },
		{ QStringLiteral("processor_ready"), processor && processor->isReady() },
		{ QStringLiteral("active_model_id"), processor ? processor->activeModelId() : QString() },
		{ QStringLiteral("active_model_path"), processor ? processor->activeModelPath() : QString() },
		{ QStringLiteral("used_fallback"), processor && processor->usedFallback() },
		{ QStringLiteral("reported_latency_samples"),
		  processor ? static_cast< int >(processor->latencySamples()) : 0 },
		{ QStringLiteral("error"), errorMessage }
	};

	QString writeError;
	if (!writeJsonAtomically(environmentValue(ENV_INPUT_DONE), result, &writeError)) {
		qWarning("SpeechCleanupTestAudioInput: could not write completion artifact: %s", qUtf8Printable(writeError));
	}
}

void SpeechCleanupTestAudioInput::run() {
	iMicChannels = 1;
	iMicFreq     = TEST_SAMPLE_RATE;
	eMicFormat   = SampleFloat;
	iEchoChannels = 0;
	iEchoFreq     = TEST_SAMPLE_RATE;
	eEchoFormat   = SampleFloat;
	initializeMixer();

	const QString inputPath = environmentValue(ENV_INPUT_WAV);
	if (inputPath.isEmpty()) {
		// Receiver-only processes still need a live, deterministic input backend.
		while (bRunning) {
			QThread::msleep(20);
		}
		return;
	}

	const QString startGatePath = environmentValue(ENV_START_GATE);
	const QString inputDonePath = environmentValue(ENV_INPUT_DONE);
	if (startGatePath.isEmpty() || inputDonePath.isEmpty()) {
		const QString error = QStringLiteral("Sender requires start-gate and input-done paths");
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning) QThread::msleep(20);
		return;
	}

	while (bRunning) {
		const ServerHandlerPtr server = Global::get().serverHandlerSnapshot();
		if (server && server->hasSynchronized() && gateMatchesRunToken(startGatePath)) {
			break;
		}
		QThread::msleep(20);
	}
	if (!bRunning) {
		return;
	}

	QFileInfo inputInfo(inputPath);
	if (!inputInfo.isFile() || !inputInfo.isReadable()) {
		const QString error = QStringLiteral("Input WAV is not a readable regular file: %1").arg(inputPath);
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning) QThread::msleep(20);
		return;
	}

	SF_INFO info {};
	SNDFILE *inputFile = openSoundFile(inputPath, SFM_READ, &info);
	if (!inputFile) {
		const QString error = QStringLiteral("Could not open input WAV: %1").arg(QString::fromLocal8Bit(sf_strerror(nullptr)));
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning) QThread::msleep(20);
		return;
	}

	if (info.samplerate != static_cast< int >(TEST_SAMPLE_RATE) || info.channels != 1) {
		const QString error = QStringLiteral("Input WAV must be mono 48000 Hz (got %1 channels at %2 Hz)")
									  .arg(info.channels)
									  .arg(info.samplerate);
		sf_close(inputFile);
		qWarning("SpeechCleanupTestAudioInput: %s", qUtf8Printable(error));
		writeDone(false, error, 0, 0);
		while (bRunning) QThread::msleep(20);
		return;
	}

	qInfo("SpeechCleanupTestAudioInput: streaming %s", qUtf8Printable(inputPath));
	std::array< float, TEST_FRAME_SIZE > frame {};
	std::uint64_t sourceFrames    = 0;
	std::uint64_t submittedFrames = 0;
	auto nextDeadline             = std::chrono::steady_clock::now();

	// The tuning loop may request a transmitted silent pre-roll so the generic
	// Opus/jitter startup transient is kept separate from cleanup quality. The
	// receiver discards exactly these frames from the capture artifact; this is
	// independent of the zero-tail setting used to prove causal drain behavior.
	frame.fill(0.0f);
	for (int index = 0; bRunning && index < configuredPreRollFrames(); ++index) {
		addMic(frame.data(), static_cast< unsigned int >(frame.size()));
		submittedFrames += frame.size();
		paceRealtime(nextDeadline);
	}

	while (bRunning) {
		frame.fill(0.0f);
		const sf_count_t readFrames = sf_readf_float(inputFile, frame.data(), static_cast< sf_count_t >(frame.size()));
		if (readFrames < 0) {
			const QString error = QStringLiteral("Failed while reading input WAV: %1")
									  .arg(QString::fromLocal8Bit(sf_strerror(inputFile)));
			sf_close(inputFile);
			writeDone(false, error, sourceFrames, submittedFrames);
			while (bRunning) QThread::msleep(20);
			return;
		}
		if (readFrames == 0) {
			break;
		}

		sourceFrames += static_cast< std::uint64_t >(readFrames);
		addMic(frame.data(), static_cast< unsigned int >(frame.size()));
		submittedFrames += frame.size();
		paceRealtime(nextDeadline);
	}

	sf_close(inputFile);

	frame.fill(0.0f);
	for (int index = 0; bRunning && index < configuredTailFrames(); ++index) {
		addMic(frame.data(), static_cast< unsigned int >(frame.size()));
		submittedFrames += frame.size();
		paceRealtime(nextDeadline);
	}
	if (bRunning) {
		// Continuous mode is used to preserve the full corpus waveform. End it
		// explicitly so the receiver observes a real Opus terminator and exercises
		// causal cleanup-tail draining instead of aging out through jitter timeout.
		m_drainedCleanupSamples = finishSpeechCleanupE2ETransmission();
		m_terminatorSubmitted = true;
	}

	if (bRunning) {
		qInfo("SpeechCleanupTestAudioInput: completed %llu source frames and %llu submitted frames",
			  static_cast< unsigned long long >(sourceFrames), static_cast< unsigned long long >(submittedFrames));
		writeDone(true, {}, sourceFrames, submittedFrames);
	}

	while (bRunning) {
		QThread::msleep(20);
	}
}

SpeechCleanupTestAudioOutput::SpeechCleanupTestAudioOutput()
	: m_capturePath(environmentValue(ENV_CAPTURE_WAV)), m_captureSender(environmentValue(ENV_CAPTURE_SENDER)),
	  m_captureFramesToSkip(static_cast< std::uint64_t >(configuredPreRollFrames()) * TEST_FRAME_SIZE) {
	connect(this, &AudioOutput::audioSourceFetched, this, &SpeechCleanupTestAudioOutput::captureSource,
			Qt::DirectConnection);
}

SpeechCleanupTestAudioOutput::~SpeechCleanupTestAudioOutput() {
	bRunning = false;
	wait();
}

bool SpeechCleanupTestAudioOutput::openCapture(QString *errorMessage) {
	if (m_capturePath.isEmpty()) {
		return true;
	}
	if (m_captureSender.isEmpty()) {
		if (errorMessage) *errorMessage = QStringLiteral("Capture sender name is required");
		return false;
	}
	if (environmentValue(ENV_STOP_GATE).isEmpty() || environmentValue(ENV_CAPTURE_DONE).isEmpty()) {
		if (errorMessage) *errorMessage = QStringLiteral("Capture requires stop-gate and completion paths");
		return false;
	}

	const QFileInfo captureInfo(m_capturePath);
	if (!QDir().mkpath(captureInfo.absolutePath())) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not create capture directory '%1'").arg(captureInfo.absolutePath());
		}
		return false;
	}

	SF_INFO info {};
	info.samplerate = static_cast< int >(TEST_SAMPLE_RATE);
	info.channels   = 1;
	info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	m_captureFile   = openSoundFile(m_capturePath, SFM_WRITE, &info);
	if (!m_captureFile) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Could not open capture WAV: %1")
							.arg(QString::fromLocal8Bit(sf_strerror(nullptr)));
		}
		return false;
	}

	m_monoBuffer.resize(TEST_FRAME_SIZE);
	qInfo("SpeechCleanupTestAudioOutput: capturing sender '%s' to %s", qUtf8Printable(m_captureSender),
		  qUtf8Printable(m_capturePath));
	return true;
}

void SpeechCleanupTestAudioOutput::closeCapture() {
	if (m_captureFile) {
		sf_write_sync(m_captureFile);
		sf_close(m_captureFile);
		m_captureFile = nullptr;
	}
}

void SpeechCleanupTestAudioOutput::observeSpeechCleanupSourceForE2E(const AudioOutputSpeech *speech) {
	if (!speech || !speech->p || speech->p->qsName != m_captureSender) {
		return;
	}

	const bool wasApplied = speech->remoteSpeechCleanupWasAppliedForE2E();
	m_remoteCleanupDiagnostics.drainedSamples =
		static_cast< int >(speech->remoteSpeechCleanupDrainedSamplesForE2E());
	m_remoteCleanupDiagnostics.drainCompleted = speech->remoteSpeechCleanupDrainCompletedForE2E();
	// Capture the first matching source even when cleanup is unavailable or
	// bypassed, then refresh exactly once if it later becomes active. This keeps
	// allocations out of the steady-state mixer callback while preserving useful
	// failure diagnostics.
	if (m_remoteCleanupDiagnostics.captured
		&& (!wasApplied || m_remoteCleanupDiagnostics.wasApplied)) {
		return;
	}

	const Mumble::SpeechCleanup::Selection &selection = speech->remoteSpeechCleanupSelectionForE2E();
	const SpeechCleanupProcessor *processor = speech->remoteSpeechCleanupProcessorForE2E();
	const bool processorReady = processor && processor->isReady();
	const bool active = speech->remoteSpeechCleanupActiveForE2E();

	m_remoteCleanupDiagnostics.captured         = true;
	m_remoteCleanupDiagnostics.requestedEnabled = speech->remoteSpeechCleanupRequestedForE2E();
	m_remoteCleanupDiagnostics.requestedBackend =
		QString::fromLatin1(Mumble::SpeechCleanup::backendDisplayName(selection.backend));
	m_remoteCleanupDiagnostics.requestedModelId = selection.modelId;
	m_remoteCleanupDiagnostics.effectiveBackend =
		processorReady && active
			? QString::fromLatin1(Mumble::SpeechCleanup::backendDisplayName(selection.backend))
			: QString();
	m_remoteCleanupDiagnostics.effectiveModelId = processorReady ? processor->activeModelId() : QString();
	m_remoteCleanupDiagnostics.processorReady   = processorReady;
	m_remoteCleanupDiagnostics.activeModelId    = processor ? processor->activeModelId() : QString();
	m_remoteCleanupDiagnostics.activeModelPath  = processor ? processor->activeModelPath() : QString();
	m_remoteCleanupDiagnostics.usedFallback     = processor && processor->usedFallback();
	m_remoteCleanupDiagnostics.reportedLatencySamples =
		processor ? static_cast< int >(processor->latencySamples()) : 0;
	m_remoteCleanupDiagnostics.active     = active;
	m_remoteCleanupDiagnostics.wasApplied = wasApplied;
	m_remoteCleanupDiagnostics.preset =
		remoteSpeechCleanupPresetName(speech->remoteSpeechCleanupPresetForE2E());
	m_remoteCleanupDiagnostics.mixFactor = speech->remoteSpeechCleanupMixFactorForE2E();
}

void SpeechCleanupTestAudioOutput::captureSource(float *outputPCM, unsigned int sampleCount,
											 unsigned int channelCount, unsigned int sampleRate, bool isSpeech,
											 const ClientUser *user) {
	if (!m_captureFile || !outputPCM || !isSpeech || !user || sampleCount == 0 || channelCount == 0
		|| sampleRate != TEST_SAMPLE_RATE || user->qsName != m_captureSender) {
		return;
	}

	const unsigned int skippedFrames = static_cast< unsigned int >(
		std::min< std::uint64_t >(m_captureFramesToSkip, sampleCount));
	m_captureFramesToSkip -= skippedFrames;
	m_discardedPreRollFrames += skippedFrames;
	if (skippedFrames == sampleCount) {
		return;
	}

	const unsigned int capturedFrames = sampleCount - skippedFrames;
	if (m_monoBuffer.size() < capturedFrames) {
		m_monoBuffer.resize(capturedFrames);
	}

	for (unsigned int frameIndex = 0; frameIndex < capturedFrames; ++frameIndex) {
		float mixed = 0.0f;
		for (unsigned int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
			mixed += outputPCM[(frameIndex + skippedFrames) * channelCount + channelIndex];
		}
		m_monoBuffer[frameIndex] = mixed / static_cast< float >(channelCount);
	}

	const sf_count_t written =
		sf_writef_float(m_captureFile, m_monoBuffer.data(), static_cast< sf_count_t >(capturedFrames));
	if (written != static_cast< sf_count_t >(capturedFrames)) {
		qWarning("SpeechCleanupTestAudioOutput: short capture write (%lld of %u frames)",
				 static_cast< long long >(written), capturedFrames);
		return;
	}

	m_capturedFrames += capturedFrames;
	++m_captureCallbacks;
}

void SpeechCleanupTestAudioOutput::writeDone(bool ok, const QString &errorMessage, bool stopGateObserved) const {
	if (m_capturePath.isEmpty()) {
		return;
	}

	const QJsonObject remoteCleanup {
		{ QStringLiteral("diagnostics_captured"), m_remoteCleanupDiagnostics.captured },
		{ QStringLiteral("requested_enabled"), m_remoteCleanupDiagnostics.requestedEnabled },
		{ QStringLiteral("requested_backend"), m_remoteCleanupDiagnostics.requestedBackend },
		{ QStringLiteral("requested_model_id"), m_remoteCleanupDiagnostics.requestedModelId },
		{ QStringLiteral("effective_backend"), m_remoteCleanupDiagnostics.effectiveBackend },
		{ QStringLiteral("effective_model_id"), m_remoteCleanupDiagnostics.effectiveModelId },
		{ QStringLiteral("processor_ready"), m_remoteCleanupDiagnostics.processorReady },
		{ QStringLiteral("active_model_id"), m_remoteCleanupDiagnostics.activeModelId },
		{ QStringLiteral("active_model_path"), m_remoteCleanupDiagnostics.activeModelPath },
		{ QStringLiteral("used_fallback"), m_remoteCleanupDiagnostics.usedFallback },
		{ QStringLiteral("reported_latency_samples"), m_remoteCleanupDiagnostics.reportedLatencySamples },
		{ QStringLiteral("reported_latency_ms"),
		  static_cast< double >(m_remoteCleanupDiagnostics.reportedLatencySamples) * 1000.0
			  / static_cast< double >(TEST_SAMPLE_RATE) },
		{ QStringLiteral("active"), m_remoteCleanupDiagnostics.active },
		{ QStringLiteral("was_applied"), m_remoteCleanupDiagnostics.wasApplied },
		{ QStringLiteral("drained_samples"), m_remoteCleanupDiagnostics.drainedSamples },
		{ QStringLiteral("drain_completed"), m_remoteCleanupDiagnostics.drainCompleted },
		{ QStringLiteral("preset"), m_remoteCleanupDiagnostics.preset },
		{ QStringLiteral("mix_factor"), m_remoteCleanupDiagnostics.mixFactor },
	};

	QJsonObject result {
		{ QStringLiteral("ok"), ok },
		{ QStringLiteral("role"), QStringLiteral("capture") },
		{ QStringLiteral("completed_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
		{ QStringLiteral("capture_wav"), m_capturePath },
		{ QStringLiteral("capture_sender"), m_captureSender },
		{ QStringLiteral("sample_rate"), static_cast< int >(TEST_SAMPLE_RATE) },
		{ QStringLiteral("channels"), 1 },
		{ QStringLiteral("captured_frames"), QString::number(m_capturedFrames) },
		{ QStringLiteral("capture_callbacks"), QString::number(m_captureCallbacks) },
		{ QStringLiteral("discarded_pre_roll_frames"), QString::number(m_discardedPreRollFrames) },
		{ QStringLiteral("stop_gate_observed"), stopGateObserved },
		{ QStringLiteral("remote_cleanup"), remoteCleanup },
		{ QStringLiteral("error"), errorMessage }
	};

	QString writeError;
	if (!writeJsonAtomically(environmentValue(ENV_CAPTURE_DONE), result, &writeError)) {
		qWarning("SpeechCleanupTestAudioOutput: could not write completion artifact: %s", qUtf8Printable(writeError));
	}
}

void SpeechCleanupTestAudioOutput::run() {
	eSampleFormat = SampleFloat;
	iMixerFreq    = TEST_SAMPLE_RATE;
	iChannels     = 2;
	setBufferSize(TEST_FRAME_SIZE);
	const unsigned int channelMasks[] = { SPEAKER_FRONT_LEFT, SPEAKER_FRONT_RIGHT };
	initializeMixer(channelMasks);

	QString captureError;
	if (!openCapture(&captureError)) {
		qWarning("SpeechCleanupTestAudioOutput: %s", qUtf8Printable(captureError));
		writeDone(false, captureError, false);
		while (bRunning) QThread::msleep(20);
		return;
	}

	std::array< float, TEST_FRAME_SIZE * 2 > output {};
	auto nextDeadline = std::chrono::steady_clock::now();
	bool stopGateObserved = false;

	while (bRunning) {
		output.fill(0.0f);
		mix(output.data(), TEST_FRAME_SIZE);

		if (!m_capturePath.isEmpty() && gateMatchesRunToken(environmentValue(ENV_STOP_GATE))) {
			stopGateObserved = true;
			break;
		}
		paceRealtime(nextDeadline);
	}

	closeCapture();
	if (!m_capturePath.isEmpty()) {
		const bool ok = stopGateObserved && m_capturedFrames > 0;
		QString error;
		if (!stopGateObserved) {
			error = QStringLiteral("Capture stopped before the stop gate was observed");
		} else if (m_capturedFrames == 0) {
			error = QStringLiteral("No matching speech frames were captured");
		}
		writeDone(ok, error, stopGateObserved);
		qInfo("SpeechCleanupTestAudioOutput: finalized %llu frames from %llu callbacks",
			  static_cast< unsigned long long >(m_capturedFrames),
			  static_cast< unsigned long long >(m_captureCallbacks));
	}

	while (bRunning) {
		QThread::msleep(20);
	}
}
