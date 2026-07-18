// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationPlayback.h"

#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#	include "win.h"
#endif

#if defined(Q_OS_WIN) && defined(USE_WASAPI)
#	include <audioclient.h>
#	include <avrt.h>
#	include <ksmedia.h>
#	include <mmdeviceapi.h>
#	include <mmreg.h>
#endif

namespace Mumble::InputEnhancement {
namespace {

	using Playback = CalibrationPlayback;

	void secureWipe(std::vector< float > &samples) noexcept {
		if (samples.empty()) {
			return;
		}
#ifdef Q_OS_WIN
		SecureZeroMemory(samples.data(), samples.size() * sizeof(float));
#else
		volatile float *cursor = samples.data();
		for (std::size_t index = 0; index < samples.size(); ++index) {
			cursor[index] = 0.0f;
		}
#endif
	}

	class SecureClip final : public Playback::Clip {
	public:
		explicit SecureClip(const std::span< const float > input)
			: m_sampleCount(input.size()), m_samples(input.begin(), input.end()) {}
		~SecureClip() override { cancelAndWipe(); }

		SecureClip(const SecureClip &)            = delete;
		SecureClip &operator=(const SecureClip &) = delete;

		std::size_t sampleCount() const noexcept override { return m_sampleCount; }

		bool writeInterleavedStereo(const std::size_t offset, const std::span< float > destination,
									const float gain) const noexcept override {
			if (destination.empty() || destination.size() % 2U != 0U
				|| destination.size() / 2U > Playback::maximumQueueFrames || !std::isfinite(gain)) {
				return false;
			}
			const std::size_t frameCount = destination.size() / 2U;
			const std::lock_guard lock(m_mutex);
			if (m_cancelled || offset > m_samples.size() || frameCount > m_samples.size() - offset) {
				return false;
			}
			const float boundedGain = std::clamp(gain, 0.0f, 2.0f);
			for (std::size_t frame = 0; frame < frameCount; ++frame) {
				const float sample = std::clamp(m_samples[offset + frame] * boundedGain, -1.0f, 1.0f);
				destination[frame * 2U]     = sample;
				destination[frame * 2U + 1] = sample;
			}
			return true;
		}

		void cancelAndWipe() noexcept {
			const std::lock_guard lock(m_mutex);
			m_cancelled = true;
			secureWipe(m_samples);
			m_samples.clear();
		}

	private:
		const std::size_t m_sampleCount;
		mutable std::mutex m_mutex;
		mutable std::vector< float > m_samples;
		mutable bool m_cancelled = false;
	};

	class UnsupportedBackend final : public Playback::Backend {
	public:
		Playback::StartResult run(const Playback::Clip &, const Playback::Target &, std::stop_token,
								  Playback::ReadyCallback ready) override {
			const Playback::StartResult result{ Playback::Error::UnsupportedPlatform, false };
			ready(result);
			return result;
		}
	};

#if defined(Q_OS_WIN) && defined(USE_WASAPI)

	template< typename T > class ComOwner final {
	public:
		ComOwner() = default;
		~ComOwner() { reset(); }

		ComOwner(const ComOwner &)            = delete;
		ComOwner &operator=(const ComOwner &) = delete;

		T *get() const noexcept { return m_value; }
		T *operator->() const noexcept { return m_value; }
		T **put() noexcept {
			reset();
			return &m_value;
		}
		void **putVoid() noexcept { return reinterpret_cast< void ** >(put()); }
		void reset() noexcept {
			if (m_value) {
				m_value->Release();
				m_value = nullptr;
			}
		}

	private:
		T *m_value = nullptr;
	};

	class EventOwner final {
	public:
		EventOwner() : m_value(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
		~EventOwner() {
			if (m_value) {
				CloseHandle(m_value);
			}
		}

		EventOwner(const EventOwner &)            = delete;
		EventOwner &operator=(const EventOwner &) = delete;

		HANDLE get() const noexcept { return m_value; }
		explicit operator bool() const noexcept { return m_value != nullptr; }

	private:
		HANDLE m_value = nullptr;
	};

	class ComApartment final {
	public:
		ComApartment() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
		~ComApartment() {
			if (SUCCEEDED(m_result)) {
				CoUninitialize();
			}
		}

		bool ready() const noexcept { return SUCCEEDED(m_result); }

	private:
		HRESULT m_result;
	};

	class MmcssRegistration final {
	public:
		MmcssRegistration() : m_handle(AvSetMmThreadCharacteristicsW(L"Pro Audio", &m_taskIndex)) {
			if (!m_handle) {
				qWarning("Input calibration WASAPI: unable to register the render worker with MMCSS");
			}
		}
		~MmcssRegistration() {
			if (m_handle) {
				AvRevertMmThreadCharacteristics(m_handle);
			}
		}

		MmcssRegistration(const MmcssRegistration &)            = delete;
		MmcssRegistration &operator=(const MmcssRegistration &) = delete;

	private:
		DWORD m_taskIndex = 0;
		HANDLE m_handle   = nullptr;
	};

	ERole endpointRole(const Playback::EndpointRole role) noexcept {
		switch (role) {
			case Playback::EndpointRole::Console:
				return eConsole;
			case Playback::EndpointRole::Multimedia:
				return eMultimedia;
			case Playback::EndpointRole::Communications:
			default:
				return eCommunications;
		}
	}

	class WasapiBackend final : public Playback::Backend {
	public:
		Playback::StartResult run(const Playback::Clip &mono48k, const Playback::Target &target,
								  const std::stop_token stopToken, Playback::ReadyCallback ready) override {
			auto fail = [&ready](const Playback::Error error, const bool fallback = false) {
				const Playback::StartResult result{ error, fallback };
				ready(result);
				return result;
			};

			const QString backend = target.outputBackend.trimmed();
			if (!backend.isEmpty() && backend.compare(QStringLiteral("WASAPI"), Qt::CaseInsensitive) != 0) {
				return fail(Playback::Error::UnsupportedBackend);
			}
			if (target.exclusiveOutput) {
				return fail(Playback::Error::ExclusiveOutput);
			}
			if (!std::isfinite(target.gain) || target.gain < 0.0f) {
				return fail(Playback::Error::BackendFailure);
			}

			ComApartment apartment;
			if (!apartment.ready()) {
				return fail(Playback::Error::BackendFailure);
			}
			MmcssRegistration mmcss;
			if (stopToken.stop_requested()) {
				return { Playback::Error::None, false };
			}

			ComOwner< IMMDeviceEnumerator > enumerator;
			HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
										  __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
			if (FAILED(hr) || !enumerator.get()) {
				qWarning("Input calibration WASAPI: device enumerator failed: hr=0x%08lx", hr);
				return fail(Playback::Error::DeviceUnavailable);
			}

			const bool configuredEndpoint = !target.endpointId.isEmpty();
			bool usedDefaultFallback      = false;
			bool retryConsumed            = false;
			ComOwner< IMMDevice > device;
			auto openDefaultDevice = [&]() {
				device.reset();
				return enumerator->GetDefaultAudioEndpoint(eRender, endpointRole(target.role), device.put());
			};
			if (configuredEndpoint) {
				hr = enumerator->GetDevice(reinterpret_cast< LPCWSTR >(target.endpointId.utf16()), device.put());
				DWORD state = 0;
				if (FAILED(hr) || !device.get() || FAILED(device->GetState(&state))
					|| (state & DEVICE_STATE_ACTIVE) == 0) {
					usedDefaultFallback = true;
					device.reset();
				}
			}
			if (!device.get()) {
				hr = openDefaultDevice();
				if (hr == AUDCLNT_E_DEVICE_INVALIDATED && !retryConsumed) {
					retryConsumed = true;
					hr            = openDefaultDevice();
				}
			}
			if (FAILED(hr) || !device.get()) {
				qWarning("Input calibration WASAPI: render endpoint unavailable: hr=0x%08lx", hr);
				return fail(Playback::Error::DeviceUnavailable, usedDefaultFallback);
			}
			if (stopToken.stop_requested()) {
				return { Playback::Error::None, usedDefaultFallback };
			}

			ComOwner< IAudioClient > audioClient;
			ComOwner< IAudioRenderClient > renderClient;
			auto activateDevice = [&]() {
				renderClient.reset();
				audioClient.reset();
				return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, audioClient.putVoid());
			};
			auto activateDefault = [&]() {
				HRESULT fallbackHr = openDefaultDevice();
				if (SUCCEEDED(fallbackHr) && device.get()) {
					fallbackHr = activateDevice();
				}
				return fallbackHr;
			};
			auto activateInitialDefaultFallback = [&]() {
				if (!configuredEndpoint || usedDefaultFallback) {
					return E_FAIL;
				}
				usedDefaultFallback = true;
				return activateDefault();
			};
			auto reopenDefaultAfterInvalidation = [&]() {
				if (retryConsumed) {
					return static_cast< HRESULT >(AUDCLNT_E_DEVICE_INVALIDATED);
				}
				retryConsumed = true;
				if (configuredEndpoint) {
					usedDefaultFallback = true;
				}
				return activateDefault();
			};
			hr = activateDevice();
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
				hr = reopenDefaultAfterInvalidation();
			} else if ((FAILED(hr) || !audioClient.get()) && configuredEndpoint && !usedDefaultFallback) {
				hr = activateInitialDefaultFallback();
			}
			if (FAILED(hr) || !audioClient.get()) {
				qWarning("Input calibration WASAPI: audio client activation failed: hr=0x%08lx", hr);
				return fail(Playback::Error::DeviceUnavailable, usedDefaultFallback);
			}
			if (stopToken.stop_requested()) {
				return { Playback::Error::None, usedDefaultFallback };
			}

			WAVEFORMATEXTENSIBLE format        = {};
			format.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
			format.Format.nChannels            = 2;
			format.Format.nSamplesPerSec       = Playback::sampleRateHz;
			format.Format.wBitsPerSample       = 32;
			format.Format.nBlockAlign          = format.Format.nChannels * sizeof(float);
			format.Format.nAvgBytesPerSec      = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
			format.Format.cbSize               = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
			format.Samples.wValidBitsPerSample = 32;
			format.dwChannelMask               = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
			format.SubFormat                   = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

			// These public flags have existed since Windows 7. Numeric constants keep
			// compilation compatible with older MinGW SDK headers that omit the names.
			constexpr DWORD autoConvertPcm    = 0x80000000UL;
			constexpr DWORD srcDefaultQuality = 0x08000000UL;
			const DWORD streamFlags =
				AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST | autoConvertPcm | srcDefaultQuality;
			auto initializeClient = [&]() {
				return audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, &format.Format, nullptr);
			};
			hr = initializeClient();
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
				const HRESULT fallbackHr = reopenDefaultAfterInvalidation();
				hr                       = SUCCEEDED(fallbackHr) && audioClient.get() ? initializeClient() : fallbackHr;
			}
			if (FAILED(hr)) {
				qWarning("Input calibration WASAPI: shared float format unavailable: hr=0x%08lx", hr);
				const Playback::Error error = hr == AUDCLNT_E_DEVICE_IN_USE ? Playback::Error::ExclusiveOutput
																			: Playback::Error::UnsupportedFormat;
				return fail(error, usedDefaultFallback);
			}

			EventOwner event;
			if (!event) {
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}
			UINT32 bufferFrameCount = 0;
			auto prepareRenderServices = [&]() {
				renderClient.reset();
				bufferFrameCount  = 0;
				HRESULT prepareHr = audioClient->SetEventHandle(event.get());
				if (SUCCEEDED(prepareHr)) {
					prepareHr = audioClient->GetBufferSize(&bufferFrameCount);
				}
				if (SUCCEEDED(prepareHr) && bufferFrameCount == 0) {
					prepareHr = E_FAIL;
				}
				if (SUCCEEDED(prepareHr)) {
					prepareHr = audioClient->GetService(__uuidof(IAudioRenderClient), renderClient.putVoid());
				}
				if (SUCCEEDED(prepareHr) && !renderClient.get()) {
					prepareHr = E_FAIL;
				}
				return prepareHr;
			};
			hr = prepareRenderServices();
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
				const HRESULT fallbackHr = reopenDefaultAfterInvalidation();
				if (SUCCEEDED(fallbackHr) && audioClient.get()) {
					hr = initializeClient();
					if (SUCCEEDED(hr)) {
						hr = prepareRenderServices();
					}
				} else {
					hr = fallbackHr;
				}
			}
			if (FAILED(hr) || !renderClient.get() || bufferFrameCount == 0) {
				qWarning("Input calibration WASAPI: render service setup failed: hr=0x%08lx", hr);
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}
			std::stop_callback wakeOnStop(stopToken, [handle = event.get()] { SetEvent(handle); });

			std::size_t cursor = 0;
			const float gain   = std::clamp(target.gain, 0.0f, 2.0f);
			struct QueueResult final {
				HRESULT result = S_OK;
				bool cancelled = false;
			};
			auto queueFrames = [&](const UINT32 frameCount) -> QueueResult {
				BYTE *bytes          = nullptr;
				const HRESULT result = renderClient->GetBuffer(frameCount, &bytes);
				if (FAILED(result)) {
					return { result, false };
				}

				const std::span< float > interleaved(reinterpret_cast< float * >(bytes),
												static_cast< std::size_t >(frameCount) * 2U);
				if (!mono48k.writeInterleavedStereo(cursor, interleaved, gain)) {
					return { renderClient->ReleaseBuffer(frameCount, AUDCLNT_BUFFERFLAGS_SILENT), true };
				}
				cursor += frameCount;
				return { renderClient->ReleaseBuffer(frameCount, 0), false };
			};

			auto queueInitialFrames = [&]() {
				const UINT32 initialFrames = static_cast< UINT32 >(
					std::min({ static_cast< std::size_t >(bufferFrameCount), mono48k.sampleCount(),
							   Playback::maximumQueueFrames }));
				return queueFrames(initialFrames);
			};
			if (stopToken.stop_requested()) {
				return { Playback::Error::None, usedDefaultFallback };
			}
			QueueResult initialQueue = queueInitialFrames();
			if (FAILED(initialQueue.result)) {
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}
			if (initialQueue.cancelled || stopToken.stop_requested()) {
				audioClient->Reset();
				return { Playback::Error::None, usedDefaultFallback };
			}

			hr = audioClient->Start();
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
				const HRESULT fallbackHr = reopenDefaultAfterInvalidation();
				if (SUCCEEDED(fallbackHr) && audioClient.get()) {
					hr = initializeClient();
					if (SUCCEEDED(hr)) {
						hr = prepareRenderServices();
					}
					if (SUCCEEDED(hr)) {
						cursor       = 0;
						initialQueue = queueInitialFrames();
						hr = initialQueue.cancelled ? E_ABORT : initialQueue.result;
					}
					if (SUCCEEDED(hr)) {
						hr = audioClient->Start();
					}
				} else {
					hr = fallbackHr;
				}
			}
			if (initialQueue.cancelled || stopToken.stop_requested()) {
				audioClient->Stop();
				audioClient->Reset();
				return { Playback::Error::None, usedDefaultFallback };
			}
			if (FAILED(hr)) {
				qWarning("Input calibration WASAPI: stream start failed: hr=0x%08lx", hr);
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}
			if (stopToken.stop_requested()) {
				audioClient->Stop();
				audioClient->Reset();
				return { Playback::Error::None, usedDefaultFallback };
			}

			ready({ Playback::Error::None, usedDefaultFallback });

			Playback::StartResult completion{ Playback::Error::None, usedDefaultFallback };
			while (!stopToken.stop_requested()) {
				UINT32 padding = 0;
				hr             = audioClient->GetCurrentPadding(&padding);
				if (FAILED(hr) || padding > bufferFrameCount) {
					completion.error = Playback::Error::BackendFailure;
					break;
				}
				if (cursor == mono48k.sampleCount() && padding == 0) {
					break;
				}

				const UINT32 available = bufferFrameCount - padding;
				if (available > 0 && cursor < mono48k.sampleCount()) {
					const UINT32 toWrite = static_cast< UINT32 >(
						std::min({ static_cast< std::size_t >(available), mono48k.sampleCount() - cursor,
								   Playback::maximumQueueFrames }));
					const QueueResult queued = queueFrames(toWrite);
					if (FAILED(queued.result)) {
						completion.error = Playback::Error::BackendFailure;
						break;
					}
					if (queued.cancelled) {
						break;
					}
				}

				const DWORD waitResult = WaitForSingleObject(event.get(), 100);
				if (waitResult == WAIT_FAILED) {
					completion.error = Playback::Error::BackendFailure;
					break;
				}
			}

			audioClient->Stop();
			audioClient->Reset();
			return completion;
		}
	};

#endif // Q_OS_WIN && USE_WASAPI

} // namespace

struct CalibrationPlayback::EventRelay final {
	explicit EventRelay(CalibrationPlayback *playback) : owner(playback) {}

	void close() noexcept {
		const std::lock_guard lock(mutex);
		closed = true;
	}

	std::mutex mutex;
	QPointer< CalibrationPlayback > owner;
	bool closed = false;
};

struct CalibrationPlayback::RunControl final {
	RunControl(std::shared_ptr< SecureClip > ownedClip, const quint64 runGeneration)
		: clip(std::move(ownedClip)), generation(runGeneration) {}
	void markExited() noexcept {
		{
			const std::lock_guard lock(exitMutex);
			exited = true;
		}
		exitCondition.notify_all();
	}
	bool waitUntilExited(const std::chrono::steady_clock::time_point deadline) noexcept {
		std::unique_lock lock(exitMutex);
		return exitCondition.wait_until(lock, deadline, [this] { return exited; });
	}
	bool hasExited() const noexcept {
		const std::lock_guard lock(exitMutex);
		return exited;
	}

	std::shared_ptr< SecureClip > clip;
	std::stop_source stopSource;
	const quint64 generation;
	std::atomic_bool active{ true };
	mutable std::mutex exitMutex;
	std::condition_variable exitCondition;
	bool exited = false;
};

struct CalibrationPlayback::WorkerRegistry final {
	std::mutex mutex;
	std::condition_variable condition;
	bool occupied = false;
};

struct CalibrationPlayback::WorkerLease final {
	explicit WorkerLease(WorkerRegistry &workerRegistry) : registry(&workerRegistry) {}
	~WorkerLease() {
		if (!registry) {
			return;
		}
		{
			const std::lock_guard lock(registry->mutex);
			registry->occupied = false;
		}
		registry->condition.notify_all();
	}

	WorkerLease(const WorkerLease &)            = delete;
	WorkerLease &operator=(const WorkerLease &) = delete;

	WorkerRegistry *registry;
};

CalibrationPlayback::WorkerRegistry &CalibrationPlayback::workerRegistry() noexcept {
	// Detached playback workers may finish during static destruction. Intentionally
	// retain this tiny synchronization object for the process lifetime so their
	// final lease release can never address a destroyed mutex/condition variable.
	static WorkerRegistry *registry = new WorkerRegistry;
	return *registry;
}

CalibrationPlayback::CalibrationPlayback(QObject *parent) : CalibrationPlayback(createPlatformBackend(), parent) {
}

CalibrationPlayback::CalibrationPlayback(std::unique_ptr< Backend > backend, QObject *parent)
	: QObject(parent), m_backend(std::move(backend)), m_eventRelay(std::make_shared< EventRelay >(this)) {
}

CalibrationPlayback::~CalibrationPlayback() {
	if (m_eventRelay) {
		m_eventRelay->close();
	}
	stop();
	(void) waitForProcessIdle(std::chrono::milliseconds(50));
}

CalibrationPlayback::StartResult CalibrationPlayback::start(const std::span< const float > mono48k,
													const Target &target) {
	if (!m_backend || mono48k.empty() || mono48k.size() > maximumSampleCount
		|| !std::all_of(mono48k.begin(), mono48k.end(), [](const float sample) { return std::isfinite(sample); })) {
		return { Error::InvalidClip, false };
	}

	constexpr auto replacementBudget = std::chrono::milliseconds(50);
	const auto deadline               = std::chrono::steady_clock::now() + replacementBudget;
	const quint64 runGeneration = static_cast< quint64 >(m_generation.fetch_add(1, std::memory_order_acq_rel) + 1);
	const std::shared_ptr< RunControl > previousRun = m_run;
	if (previousRun) {
		previousRun->stopSource.request_stop();
		previousRun->clip->cancelAndWipe();
		previousRun->active.store(false, std::memory_order_release);
		if (!previousRun->hasExited() && !previousRun->waitUntilExited(deadline)) {
			return { Error::PlaybackBusy, false };
		}
	}

	WorkerRegistry &registry = workerRegistry();
	std::unique_lock registryLock(registry.mutex);
	if (!registry.condition.wait_until(registryLock, deadline, [&registry] { return !registry.occupied; })) {
		return { Error::PlaybackBusy, false };
	}

	std::shared_ptr< RunControl > run;
	std::shared_ptr< WorkerLease > lease;
	try {
		auto clip = std::make_shared< SecureClip >(mono48k);
		run       = std::make_shared< RunControl >(std::move(clip), runGeneration);
		lease     = std::make_shared< WorkerLease >(registry);
		registry.occupied = true;
		registryLock.unlock();

		auto backend                = m_backend;
		auto relay                  = m_eventRelay;
		m_run                       = run;
		std::thread worker(
			[backend = std::move(backend), relay = std::move(relay), run, lease, targetCopy = Target(target)]() mutable {
			try {
				const std::stop_token token = run->stopSource.get_token();
				struct ReadyState final {
					std::atomic_bool delivered{ false };
					std::atomic_bool startedSuccessfully{ false };
				};
				auto readyState = std::make_shared< ReadyState >();
				auto ready      = [relay, run, readyState](const StartResult result) {
					if (readyState->delivered.exchange(true, std::memory_order_acq_rel)) {
						return;
					}
					if (result) {
						readyState->startedSuccessfully.store(true, std::memory_order_release);
						postEvent(relay, run->generation, PlaybackEvent::Started, result);
					} else {
						postEvent(relay, run->generation, PlaybackEvent::FailedBeforeStart, result);
					}
				};
				StartResult completion{ Error::BackendFailure, false };
				try {
					completion = backend->run(*run->clip, targetCopy, token, ready);
				} catch (...) {
					completion = { Error::BackendFailure, false };
				}
				if (!readyState->delivered.load(std::memory_order_acquire) && !token.stop_requested()) {
					if (completion.error == Error::None) {
						completion.error = Error::BackendFailure;
					}
					ready(completion);
				}
				run->clip->cancelAndWipe();
				run->active.store(false, std::memory_order_release);
				if (readyState->startedSuccessfully.load(std::memory_order_acquire) && !token.stop_requested()) {
					if (completion.error == Error::None) {
						postEvent(relay, run->generation, PlaybackEvent::Finished, completion);
					} else {
						postEvent(relay, run->generation, PlaybackEvent::FailedAfterStart, completion);
					}
				}
			} catch (...) {
				run->clip->cancelAndWipe();
				run->active.store(false, std::memory_order_release);
			}
			run->markExited();
			backend.reset();
			relay.reset();
			run.reset();
			targetCopy = {};
			lease.reset();
		});
		worker.detach();
		lease.reset();
	} catch (...) {
		if (registryLock.owns_lock() && registry.occupied) {
			registry.occupied = false;
			registryLock.unlock();
			registry.condition.notify_all();
		}
		if (run) {
			run->stopSource.request_stop();
			run->clip->cancelAndWipe();
			run->active.store(false, std::memory_order_release);
			run->markExited();
		}
		lease.reset();
		return { Error::ThreadStartFailed, false };
	}
	return { Error::None, false };
}

void CalibrationPlayback::stop() noexcept {
	m_generation.fetch_add(1, std::memory_order_acq_rel);
	const std::shared_ptr< RunControl > run = m_run;
	if (run) {
		run->stopSource.request_stop();
		run->clip->cancelAndWipe();
		run->active.store(false, std::memory_order_release);
	}
}

bool CalibrationPlayback::active() const noexcept {
	return m_run && m_run->generation == generation() && m_run->active.load(std::memory_order_acquire);
}

quint64 CalibrationPlayback::generation() const noexcept {
	return static_cast< quint64 >(m_generation.load(std::memory_order_acquire));
}

bool CalibrationPlayback::waitForProcessIdle(const std::chrono::milliseconds timeout) noexcept {
	WorkerRegistry &registry = workerRegistry();
	std::unique_lock lock(registry.mutex);
	return registry.condition.wait_for(lock, timeout, [&registry] { return !registry.occupied; });
}

void CalibrationPlayback::postEvent(const std::shared_ptr< EventRelay > &relay, const quint64 runGeneration,
									PlaybackEvent event, const StartResult result) {
	if (!relay) {
		return;
	}
	const std::lock_guard lock(relay->mutex);
	CalibrationPlayback *owner = relay->owner.data();
	if (relay->closed || !owner) {
		return;
	}
	QMetaObject::invokeMethod(
			owner,
		[relay, runGeneration, event, result] {
			CalibrationPlayback *owner = relay->owner.data();
			if (!owner || owner->generation() != runGeneration) {
				return;
			}
			switch (event) {
				case PlaybackEvent::Started:
					emit owner->playbackStarted(runGeneration, result.usedDefaultFallback);
					break;
				case PlaybackEvent::FailedBeforeStart:
					emit owner->playbackFailed(runGeneration, static_cast< int >(result.error), false);
					break;
				case PlaybackEvent::FailedAfterStart:
					emit owner->playbackFailed(runGeneration, static_cast< int >(result.error), true);
					break;
				case PlaybackEvent::Finished:
					emit owner->playbackFinished(runGeneration);
					break;
			}
		},
		Qt::QueuedConnection);
}

std::unique_ptr< CalibrationPlayback::Backend > CalibrationPlayback::createPlatformBackend() {
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
	return std::make_unique< WasapiBackend >();
#else
	return std::make_unique< UnsupportedBackend >();
#endif
}

} // namespace Mumble::InputEnhancement
