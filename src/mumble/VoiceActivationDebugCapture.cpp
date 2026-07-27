// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VoiceActivationDebugCapture.h"

#include <sndfile.h>

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QString>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <system_error>

namespace {

constexpr const char *kCaptureDirectoryEnvironment = "MUMBLE_VOICE_DIAGNOSTIC_DIR";
constexpr const char *kCaptureLimitEnvironment     = "MUMBLE_VOICE_DIAGNOSTIC_MAX_SECONDS";
constexpr const char *kCaptureGitHeadEnvironment   = "MUMBLE_VOICE_DIAGNOSTIC_GIT_HEAD";

std::filesystem::path pathFromQString(const QString &path) {
#ifdef Q_OS_WIN
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(path.toStdString());
#endif
}

SNDFILE *openWaveFile(const std::filesystem::path &path, SF_INFO &info) {
#ifdef Q_OS_WIN
	return sf_wchar_open(path.c_str(), SFM_WRITE, &info);
#else
	return sf_open(path.c_str(), SFM_WRITE, &info);
#endif
}

void writeSilence(SNDFILE *file, std::uint64_t samples) {
	static constexpr std::array< float, 4096 > silence = {};
	while (samples > 0) {
		const sf_count_t count =
			static_cast< sf_count_t >(std::min< std::uint64_t >(samples, silence.size()));
		sf_write_float(file, silence.data(), count);
		samples -= static_cast< std::uint64_t >(count);
	}
}

const char *boolText(bool value) {
	return value ? "1" : "0";
}

} // namespace

struct VoiceActivationDebugCapture::WriterState {
	std::filesystem::path path;
	SNDFILE *file = nullptr;
	unsigned int sampleRate = 0;
	std::uint64_t writtenSamples = 0;
	bool initializedTimeline = false;

	~WriterState() {
		if (file) {
			sf_write_sync(file);
			sf_close(file);
		}
	}
};

VoiceActivationDebugCapture &VoiceActivationDebugCapture::instance() {
	static VoiceActivationDebugCapture capture;
	return capture;
}

void VoiceActivationDebugCapture::initializeFromEnvironment() {
	(void) instance();
}

VoiceActivationDebugCapture::VoiceActivationDebugCapture() : m_startedAt(std::chrono::steady_clock::now()) {
	const QString configuredDirectory = qEnvironmentVariable(kCaptureDirectoryEnvironment).trimmed();
	if (configuredDirectory.isEmpty()) {
		return;
	}

	const QString absoluteDirectory = QDir(configuredDirectory).absolutePath();
	if (!QDir().mkpath(absoluteDirectory)) {
		qWarning() << "Voice activation diagnostic capture could not create" << absoluteDirectory;
		return;
	}

	bool limitOk          = false;
	const int limitSeconds = qEnvironmentVariableIntValue(kCaptureLimitEnvironment, &limitOk);
	if (limitOk && limitSeconds >= 10 && limitSeconds <= 3600) {
		m_maxDuration = std::chrono::seconds(limitSeconds);
	}

	m_directory   = pathFromQString(absoluteDirectory);
	m_startedAtUtc =
		QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
	m_gitHead = qEnvironmentVariable(kCaptureGitHeadEnvironment).trimmed().toStdString();
	m_queue   = std::make_unique< PendingItem[] >(kQueueCapacity);

	{
		std::ofstream notice(m_directory / "CAPTURE_NOTICE.txt", std::ios::out | std::ios::trunc);
		notice << "LOCAL VOICE ACTIVATION DIAGNOSTIC CAPTURE\n\n"
			   << "This directory can contain raw microphone audio and voices received from the Mumble server.\n"
			   << "Nothing is uploaded automatically. Treat the files as private and delete them when no longer needed.\n"
			   << "The capture stops when the client exits or after " << m_maxDuration.count() << " seconds.\n";
	}
	{
		std::ofstream activeMarker(m_directory / ".capture-active", std::ios::out | std::ios::trunc);
		activeMarker << m_startedAtUtc << '\n';
	}

	m_inputWriter         = std::make_unique< WriterState >();
	m_inputWriter->path   = m_directory / "raw-input.wav";
	m_serverWriter        = std::make_unique< WriterState >();
	m_serverWriter->path  = m_directory / "server-mix.wav";
	m_metricsFile         = std::make_unique< std::ofstream >(m_directory / "metrics.csv",
															 std::ios::out | std::ios::trunc);
	if (!m_metricsFile->is_open()) {
		qWarning() << "Voice activation diagnostic capture could not create metrics.csv in" << absoluteDirectory;
		m_metricsFile.reset();
		std::error_code error;
		std::filesystem::remove(m_directory / ".capture-active", error);
		return;
	}

	*m_metricsFile
		<< "timestamp_us,input_sample_index,input_sample_rate,raw_rms_db,raw_peak_db,processed_rms_db,"
		   "clean_rms_db,amplitude_level,speech_probability,selected_level,silence_threshold,speech_threshold,"
		   "vad_source,input_gate_mode,transmit_mode,vad_candidate,gate_allowed,gate_open,acoustic_speech,"
		   "transmitting,transmission_blocked,hold_frames,gate_attack_frames,gate_release_frames\n";
	m_metricsFile->flush();

	m_enabled.store(true, std::memory_order_release);
	m_accepting.store(true, std::memory_order_release);
	m_worker = std::thread([this] { workerLoop(); });

	qWarning().noquote()
		<< QStringLiteral("LOCAL VOICE DIAGNOSTIC CAPTURE ENABLED: %1 (raw microphone and incoming server voices; "
						  "nothing is uploaded)")
			   .arg(absoluteDirectory);
}

VoiceActivationDebugCapture::~VoiceActivationDebugCapture() {
	if (!m_enabled.load(std::memory_order_acquire)) {
		return;
	}

	m_accepting.store(false, std::memory_order_release);
	{
		std::lock_guard< std::mutex > lock(m_queueMutex);
		m_stop = true;
	}
	m_queueReady.notify_all();
	if (m_worker.joinable()) {
		m_worker.join();
	}
}

bool VoiceActivationDebugCapture::enabled() const noexcept {
	return m_accepting.load(std::memory_order_acquire);
}

std::uint64_t VoiceActivationDebugCapture::timestampMicroseconds() const noexcept {
	const auto elapsed = std::chrono::steady_clock::now() - m_startedAt;
	return static_cast< std::uint64_t >(
		std::chrono::duration_cast< std::chrono::microseconds >(elapsed).count());
}

void VoiceActivationDebugCapture::captureInputFrame(const short *samples, unsigned int sampleCount,
													 unsigned int sampleRate, const InputMetrics &metrics,
													 std::uint64_t timestampUs) noexcept {
	if (!samples || sampleCount == 0 || sampleRate == 0 || !enabled()) {
		return;
	}
	enqueueInput(samples, sampleCount, sampleRate, metrics, timestampUs);
}

void VoiceActivationDebugCapture::captureServerMix(const float *samples, unsigned int sampleCount,
													unsigned int sampleRate) noexcept {
	if (!samples || sampleCount == 0 || sampleRate == 0 || !enabled()) {
		return;
	}
	enqueueServer(samples, sampleCount, sampleRate, timestampMicroseconds());
}

bool VoiceActivationDebugCapture::beginEnqueue(PendingItem *&slot) noexcept {
	if (!enabled()) {
		return false;
	}

	const std::uint64_t elapsedUs = timestampMicroseconds();
	const std::uint64_t limitUs =
		static_cast< std::uint64_t >(m_maxDuration.count()) * 1000000ULL;
	if (elapsedUs >= limitUs) {
		m_limitReached.store(true, std::memory_order_relaxed);
		m_accepting.store(false, std::memory_order_release);
		return false;
	}

	m_queueMutex.lock();
	if (m_stop || m_queueCount >= kQueueCapacity) {
		m_queueMutex.unlock();
		return false;
	}

	slot = &m_queue[m_writeIndex];
	return true;
}

void VoiceActivationDebugCapture::finishEnqueue() noexcept {
	m_writeIndex = (m_writeIndex + 1) % kQueueCapacity;
	++m_queueCount;
	m_queueMutex.unlock();
	m_queueReady.notify_one();
}

void VoiceActivationDebugCapture::enqueueInput(const short *samples, unsigned int sampleCount,
												unsigned int sampleRate, const InputMetrics &metrics,
												std::uint64_t timestampUs) noexcept {
	unsigned int offset = 0;
	bool includeMetrics = true;
	while (offset < sampleCount) {
		PendingItem *slot = nullptr;
		if (!beginEnqueue(slot)) {
			m_droppedInputItems.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const unsigned int count = std::min< unsigned int >(
			sampleCount - offset, static_cast< unsigned int >(kMaxSamplesPerItem));
		slot->stream      = Stream::RawInput;
		slot->sampleRate  = sampleRate;
		slot->sampleCount = count;
		slot->timestampUs =
			timestampUs + (static_cast< std::uint64_t >(offset) * 1000000ULL) / sampleRate;
		slot->hasMetrics = includeMetrics;
		if (includeMetrics) {
			slot->metrics = metrics;
		}
		for (unsigned int i = 0; i < count; ++i) {
			slot->samples[i] = static_cast< float >(samples[offset + i]) / 32768.0f;
		}

		finishEnqueue();
		offset += count;
		includeMetrics = false;
	}
}

void VoiceActivationDebugCapture::enqueueServer(const float *samples, unsigned int sampleCount,
												 unsigned int sampleRate, std::uint64_t timestampUs) noexcept {
	unsigned int offset = 0;
	while (offset < sampleCount) {
		PendingItem *slot = nullptr;
		if (!beginEnqueue(slot)) {
			m_droppedServerItems.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const unsigned int count = std::min< unsigned int >(
			sampleCount - offset, static_cast< unsigned int >(kMaxSamplesPerItem));
		slot->stream      = Stream::ServerMix;
		slot->sampleRate  = sampleRate;
		slot->sampleCount = count;
		slot->timestampUs =
			timestampUs + (static_cast< std::uint64_t >(offset) * 1000000ULL) / sampleRate;
		slot->hasMetrics = false;
		std::copy_n(samples + offset, count, slot->samples.begin());

		finishEnqueue();
		offset += count;
	}
}

void VoiceActivationDebugCapture::workerLoop() noexcept {
	for (;;) {
		PendingItem item;
		{
			std::unique_lock< std::mutex > lock(m_queueMutex);
			m_queueReady.wait(lock, [this] { return m_stop || m_queueCount > 0; });
			if (m_queueCount == 0 && m_stop) {
				break;
			}

			item = m_queue[m_readIndex];
			m_readIndex = (m_readIndex + 1) % kQueueCapacity;
			--m_queueCount;
		}
		processItem(item);
	}

	if (m_metricsFile) {
		m_metricsFile->flush();
	}
	if (m_inputWriter && m_inputWriter->file) {
		sf_write_sync(m_inputWriter->file);
	}
	if (m_serverWriter && m_serverWriter->file) {
		sf_write_sync(m_serverWriter->file);
	}
	writeManifest();

	m_inputWriter.reset();
	m_serverWriter.reset();
	m_metricsFile.reset();

	std::error_code error;
	std::filesystem::remove(m_directory / ".capture-active", error);
}

void VoiceActivationDebugCapture::processItem(const PendingItem &item) noexcept {
	WriterState *writer = item.stream == Stream::RawInput ? m_inputWriter.get() : m_serverWriter.get();
	if (!writer) {
		return;
	}

	if (!writer->file) {
		SF_INFO info = {};
		info.samplerate = static_cast< int >(item.sampleRate);
		info.channels   = 1;
		info.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
		writer->file    = openWaveFile(writer->path, info);
		if (!writer->file) {
			m_hadWriteError.store(true, std::memory_order_relaxed);
			return;
		}
		writer->sampleRate = item.sampleRate;
		sf_command(writer->file, SFC_SET_CLIPPING, nullptr, SF_TRUE);
	}

	if (writer->sampleRate != item.sampleRate) {
		m_hadWriteError.store(true, std::memory_order_relaxed);
		return;
	}

	if (!writer->initializedTimeline) {
		const std::uint64_t leadingSamples =
			(item.timestampUs * static_cast< std::uint64_t >(item.sampleRate) + 500000ULL) / 1000000ULL;
		writeSilence(writer->file, leadingSamples);
		writer->writtenSamples += leadingSamples;
		writer->initializedTimeline = true;
	}

	const std::uint64_t inputSampleIndex = writer->writtenSamples;
	const sf_count_t written =
		sf_write_float(writer->file, item.samples.data(), static_cast< sf_count_t >(item.sampleCount));
	if (written != static_cast< sf_count_t >(item.sampleCount)) {
		m_hadWriteError.store(true, std::memory_order_relaxed);
	}
	writer->writtenSamples += static_cast< std::uint64_t >(std::max< sf_count_t >(written, 0));

	if (item.hasMetrics) {
		writeMetrics(item, inputSampleIndex);
	}
}

void VoiceActivationDebugCapture::writeMetrics(const PendingItem &item,
												std::uint64_t inputSampleIndex) noexcept {
	if (!m_metricsFile || !m_metricsFile->is_open()) {
		return;
	}

	const InputMetrics &m = item.metrics;
	*m_metricsFile << item.timestampUs << ',' << inputSampleIndex << ',' << item.sampleRate << ',' << std::fixed
				   << std::setprecision(6) << m.rawRmsDb << ',' << m.rawPeakDb << ',' << m.processedRmsDb << ','
				   << m.cleanRmsDb << ',' << m.amplitudeLevel << ',' << m.speechProbability << ','
				   << m.selectedLevel << ',' << m.silenceThreshold << ',' << m.speechThreshold << ','
				   << m.vadSource << ',' << m.inputGateMode << ',' << m.transmitMode << ','
				   << boolText(m.vadCandidate) << ',' << boolText(m.gateAllowed) << ',' << boolText(m.gateOpen) << ','
				   << boolText(m.acousticSpeech) << ',' << boolText(m.transmitting) << ','
				   << boolText(m.transmissionBlocked) << ',' << m.holdFrames << ',' << m.gateAttackFrames << ','
				   << m.gateReleaseFrames << '\n';
}

void VoiceActivationDebugCapture::writeManifest() noexcept {
	std::ofstream manifest(m_directory / "capture-manifest.json", std::ios::out | std::ios::trunc);
	if (!manifest.is_open()) {
		return;
	}

	const std::uint64_t inputSamples = m_inputWriter ? m_inputWriter->writtenSamples : 0;
	const unsigned int inputRate     = m_inputWriter ? m_inputWriter->sampleRate : 0;
	const std::uint64_t serverSamples = m_serverWriter ? m_serverWriter->writtenSamples : 0;
	const unsigned int serverRate     = m_serverWriter ? m_serverWriter->sampleRate : 0;

	manifest << "{\n"
			 << "  \"schema_version\": 1,\n"
			 << "  \"started_at_utc\": \"" << m_startedAtUtc << "\",\n"
			 << "  \"git_head\": \"" << m_gitHead << "\",\n"
			 << "  \"raw_input\": {\"file\": \"raw-input.wav\", \"sample_rate\": " << inputRate
			 << ", \"samples\": " << inputSamples << "},\n"
			 << "  \"server_mix\": {\"file\": \"server-mix.wav\", \"sample_rate\": " << serverRate
			 << ", \"samples\": " << serverSamples << ", \"scope\": \"incoming remote speech before device playback; "
				"local UI samples excluded\"},\n"
			 << "  \"metrics_file\": \"metrics.csv\",\n"
			 << "  \"max_duration_seconds\": " << m_maxDuration.count() << ",\n"
			 << "  \"duration_limit_reached\": "
			 << (m_limitReached.load(std::memory_order_relaxed) ? "true" : "false") << ",\n"
			 << "  \"dropped_input_items\": " << m_droppedInputItems.load(std::memory_order_relaxed) << ",\n"
			 << "  \"dropped_server_items\": " << m_droppedServerItems.load(std::memory_order_relaxed) << ",\n"
			 << "  \"write_error\": " << (m_hadWriteError.load(std::memory_order_relaxed) ? "true" : "false")
			 << "\n"
			 << "}\n";
}
