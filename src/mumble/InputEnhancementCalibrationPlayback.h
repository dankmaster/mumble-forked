// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONPLAYBACK_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONPLAYBACK_H_

#include <QtCore/QObject>
#include <QtCore/QString>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>

namespace Mumble::InputEnhancement {

/// Local-only playback for the opaque clips produced by input calibration.
///
/// This deliberately owns an independent render session instead of entering
/// Mumble's receive/mixer chain. That keeps calibration playback out of the
/// protected AudioOutput/voice-transport contract while retaining an entirely
/// in-memory A/B flow.
class CalibrationPlayback final : public QObject {
	Q_OBJECT

public:
	static constexpr unsigned int sampleRateHz      = 48'000;
	static constexpr unsigned int maximumSeconds    = 12;
	static constexpr std::size_t maximumSampleCount = static_cast< std::size_t >(sampleRateHz) * maximumSeconds;
	static constexpr unsigned int maximumQueueMilliseconds = 20;
	static constexpr std::size_t maximumQueueFrames =
		static_cast< std::size_t >(sampleRateHz) * maximumQueueMilliseconds / 1000U;

	enum class EndpointRole { Communications, Multimedia, Console };

	struct Target final {
		QString outputBackend;
		QString endpointId;
		EndpointRole role    = EndpointRole::Communications;
		float gain           = 1.0f;
		bool exclusiveOutput = false;
	};

	enum class Error {
		None,
		InvalidClip,
		UnsupportedPlatform,
		UnsupportedBackend,
		ExclusiveOutput,
		ThreadStartFailed,
		PlaybackBusy,
		DeviceUnavailable,
		UnsupportedFormat,
		BackendFailure
	};

	struct StartResult final {
		Error error              = Error::None;
		bool usedDefaultFallback = false;

		explicit operator bool() const noexcept { return error == Error::None; }
	};

	using ReadyCallback = std::function< void(StartResult) >;

	/// A cancellation-aware view of the owned calibration PCM. Backends must not
	/// retain sample pointers. writeInterleavedStereo() converts at most 20 ms
	/// directly into an already acquired render buffer and fails as soon as
	/// stop() has synchronously wiped the clip.
	class Clip {
	public:
		virtual ~Clip() = default;

		virtual std::size_t sampleCount() const noexcept = 0;
		virtual bool writeInterleavedStereo(std::size_t offset, std::span< float > destination,
										float gain) const noexcept = 0;
	};

	/// The backend interface is public only to permit deterministic tests without
	/// opening a physical output endpoint. Production uses the platform backend.
	class Backend {
	public:
		virtual ~Backend()                           = default;
		virtual StartResult run(const Clip &mono48k, const Target &target, std::stop_token stopToken,
								ReadyCallback ready) = 0;
	};

	explicit CalibrationPlayback(QObject *parent = nullptr);
	CalibrationPlayback(std::unique_ptr< Backend > backend, QObject *parent = nullptr);
	~CalibrationPlayback();

	CalibrationPlayback(const CalibrationPlayback &)            = delete;
	CalibrationPlayback(CalibrationPlayback &&)                 = delete;
	CalibrationPlayback &operator=(const CalibrationPlayback &) = delete;
	CalibrationPlayback &operator=(CalibrationPlayback &&)      = delete;

	/// Copies the finite mono clip and admits an asynchronous start before
	/// returning. Device/start failures are reported through playbackFailed, so a
	/// slow or broken driver never blocks the UI action that requested playback.
	/// The source may be wiped by CalibrationRuntimeBridge as soon as this call
	/// succeeds.
	StartResult start(std::span< const float > mono48k, const Target &target);
	/// Synchronously requests cancellation and wipes the owned PCM. This never
	/// waits for a driver/COM call; a wedged backend is allowed to unwind later.
	void stop() noexcept;
	bool active() const noexcept;
	quint64 generation() const noexcept;

	/// Waits until the process-wide detached playback worker has completed all
	/// wrapper cleanup and released its lease. This only waits on the registry
	/// condition variable; it never joins a worker or enters a backend/COM call.
	static bool waitForProcessIdle(std::chrono::milliseconds timeout) noexcept;

signals:
	void playbackStarted(quint64 generation, bool usedDefaultFallback);
	void playbackFailed(quint64 generation, int error, bool afterStart);
	void playbackFinished(quint64 generation);

private:
	enum class PlaybackEvent { Started, FailedBeforeStart, FailedAfterStart, Finished };
	struct EventRelay;
	struct RunControl;
	struct WorkerLease;
	struct WorkerRegistry;

	static std::unique_ptr< Backend > createPlatformBackend();
	static WorkerRegistry &workerRegistry() noexcept;
	static void postEvent(const std::shared_ptr< EventRelay > &relay, quint64 generation, PlaybackEvent event,
						  StartResult result);

	std::shared_ptr< Backend > m_backend;
	std::shared_ptr< RunControl > m_run;
	std::shared_ptr< EventRelay > m_eventRelay;
	std::atomic_uint64_t m_generation{ 0 };
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONPLAYBACK_H_
