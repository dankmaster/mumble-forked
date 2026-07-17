// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONPLAYBACK_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONPLAYBACK_H_

#include <QtCore/QObject>
#include <QtCore/QString>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <thread>

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

	/// The backend interface is public only to permit deterministic tests without
	/// opening a physical output endpoint. Production uses the platform backend.
	class Backend {
	public:
		virtual ~Backend()                           = default;
		virtual StartResult run(std::span< const float > mono48k, const Target &target, std::stop_token stopToken,
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
	/// Synchronously stops playback, joins the worker and wipes the owned PCM.
	void stop() noexcept;
	bool active() const noexcept;

signals:
	void playbackStarted(bool usedDefaultFallback);
	void playbackFailed(int error, bool afterStart);
	void playbackFinished();

private:
	static std::unique_ptr< Backend > createPlatformBackend();

	std::unique_ptr< Backend > m_backend;
	std::jthread m_worker;
	std::atomic_bool m_active{ false };
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTCALIBRATIONPLAYBACK_H_
