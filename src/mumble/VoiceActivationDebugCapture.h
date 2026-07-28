// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_VOICEACTIVATIONDEBUGCAPTURE_H_
#define MUMBLE_MUMBLE_VOICEACTIVATIONDEBUGCAPTURE_H_

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/// Explicitly enabled, local-only capture for diagnosing voice activation.
///
/// The in-app Audio Debug surface starts bounded captures at runtime. The
/// original MUMBLE_VOICE_DIAGNOSTIC_* environment variables remain supported
/// for unattended developer captures. File I/O is performed by a background
/// worker rather than either audio callback.
class VoiceActivationDebugCapture final {
public:
	struct StartOptions {
		std::filesystem::path directory;
		std::chrono::seconds maxDuration = std::chrono::seconds(60);
		bool captureRawInput             = true;
		bool captureServerMix            = false;
		std::string gitHead;
		/// Pre-serialized, troubleshooting-safe audio settings supplied by the
		/// settings controller. The capture layer deliberately never reads the
		/// complete application configuration.
		std::string settingsSnapshotJson;
	};

	struct Status {
		bool active           = false;
		bool finalizing       = false;
		bool limitReached     = false;
		bool writeError       = false;
		bool captureRawInput  = false;
		bool captureServerMix = false;
		std::uint64_t elapsedSeconds      = 0;
		std::uint64_t maxDurationSeconds  = 0;
		std::uint64_t droppedInputItems   = 0;
		std::uint64_t droppedServerItems  = 0;
		std::filesystem::path directory;
	};

	struct InputMetrics {
		float rawRmsDb            = -96.0f;
		float rawPeakDb           = -96.0f;
		float processedRmsDb      = -96.0f;
		float cleanRmsDb          = -96.0f;
		float amplitudeLevel      = 0.0f;
		float speechProbability   = 0.0f;
		float selectedLevel       = 0.0f;
		float silenceThreshold    = 0.0f;
		float speechThreshold     = 0.0f;
		int vadSource             = 0;
		int inputGateMode         = 0;
		int transmitMode          = 0;
		bool vadCandidate         = false;
		bool gateAllowed          = false;
		bool gateOpen             = false;
		bool acousticSpeech       = false;
		bool transmitting         = false;
		bool transmissionBlocked  = false;
		int holdFrames            = 0;
		int gateAttackFrames      = 0;
		int gateReleaseFrames     = 0;
	};

	static VoiceActivationDebugCapture &instance();

	/// Initializes the singleton on the main thread before audio callbacks begin.
	static void initializeFromEnvironment();

	bool start(const StartOptions &options);
	void stop();
	Status status() const;
	bool enabled() const noexcept;
	bool capturesRawInput() const noexcept;
	bool capturesServerMix() const noexcept;
	std::uint64_t timestampMicroseconds() const noexcept;

	void captureInputFrame(const short *samples, unsigned int sampleCount, unsigned int sampleRate,
						   const InputMetrics &metrics, std::uint64_t timestampUs) noexcept;
	void captureServerMix(const float *samples, unsigned int sampleCount, unsigned int sampleRate) noexcept;

	VoiceActivationDebugCapture(const VoiceActivationDebugCapture &)            = delete;
	VoiceActivationDebugCapture &operator=(const VoiceActivationDebugCapture &) = delete;

private:
	enum class Stream : std::uint8_t { RawInput, ServerMix };

	static constexpr std::size_t kMaxSamplesPerItem = 2048;
	static constexpr std::size_t kQueueCapacity     = 1024;

	struct PendingItem {
		Stream stream = Stream::RawInput;
		unsigned int sampleRate = 0;
		unsigned int sampleCount = 0;
		std::uint64_t timestampUs = 0;
		bool hasMetrics = false;
		InputMetrics metrics;
		std::array< float, kMaxSamplesPerItem > samples = {};
	};

	struct WriterState;

	VoiceActivationDebugCapture();
	~VoiceActivationDebugCapture();

	void enqueueInput(const short *samples, unsigned int sampleCount, unsigned int sampleRate,
					  const InputMetrics &metrics, std::uint64_t timestampUs) noexcept;
	void enqueueServer(const float *samples, unsigned int sampleCount, unsigned int sampleRate,
					   std::uint64_t timestampUs) noexcept;
	bool beginEnqueue(PendingItem *&slot) noexcept;
	void finishEnqueue() noexcept;
	void workerLoop() noexcept;
	void processItem(const PendingItem &item) noexcept;
	void writeMetrics(const PendingItem &item, std::uint64_t inputSampleIndex) noexcept;
	void writeManifest() noexcept;

	std::filesystem::path m_directory;
	std::string m_startedAtUtc;
	std::string m_gitHead;
	bool m_hasSettingsSnapshot = false;
	std::chrono::steady_clock::time_point m_startedAt;
	std::chrono::seconds m_maxDuration = std::chrono::seconds(300);
	std::atomic< std::int64_t > m_startedAtSteadyMicroseconds { 0 };
	std::atomic< bool > m_captureRawInput { true };
	std::atomic< bool > m_captureServerMix { true };

	std::atomic< bool > m_enabled { false };
	std::atomic< bool > m_accepting { false };
	std::atomic< bool > m_limitReached { false };
	std::atomic< bool > m_hadWriteError { false };
	std::atomic< std::uint64_t > m_droppedInputItems { 0 };
	std::atomic< std::uint64_t > m_droppedServerItems { 0 };
	std::atomic< std::uint64_t > m_stoppedElapsedUs { 0 };

	std::unique_ptr< PendingItem[] > m_queue;
	std::size_t m_readIndex  = 0;
	std::size_t m_writeIndex = 0;
	std::size_t m_queueCount = 0;
	std::uint64_t m_metricsInputSamples = 0;
	bool m_stop              = false;
	std::mutex m_queueMutex;
	std::condition_variable m_queueReady;
	std::thread m_worker;

	std::unique_ptr< WriterState > m_inputWriter;
	std::unique_ptr< WriterState > m_serverWriter;
	std::unique_ptr< std::ofstream > m_metricsFile;
};

#endif // MUMBLE_MUMBLE_VOICEACTIVATIONDEBUGCAPTURE_H_
