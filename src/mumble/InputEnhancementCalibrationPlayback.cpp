// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementCalibrationPlayback.h"

#include <QtCore/QDebug>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
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

	struct SecureClip final {
		explicit SecureClip(const std::span< const float > input) : samples(input.begin(), input.end()) {}
		~SecureClip() { secureWipe(samples); }

		SecureClip(const SecureClip &)            = delete;
		SecureClip &operator=(const SecureClip &) = delete;

		std::vector< float > samples;
	};

	class UnsupportedBackend final : public Playback::Backend {
	public:
		Playback::StartResult run(std::span< const float >, const Playback::Target &, std::stop_token,
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
		Playback::StartResult run(const std::span< const float > mono48k, const Playback::Target &target,
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
			}
			if (FAILED(hr) || !device.get()) {
				qWarning("Input calibration WASAPI: render endpoint unavailable: hr=0x%08lx", hr);
				return fail(Playback::Error::DeviceUnavailable, usedDefaultFallback);
			}
			if (stopToken.stop_requested()) {
				return { Playback::Error::None, usedDefaultFallback };
			}

			ComOwner< IAudioClient > audioClient;
			auto activateDevice = [&]() {
				audioClient.reset();
				return device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, audioClient.putVoid());
			};
			hr = activateDevice();
			if ((FAILED(hr) || !audioClient.get()) && configuredEndpoint && !usedDefaultFallback) {
				usedDefaultFallback = true;
				hr                  = openDefaultDevice();
				if (SUCCEEDED(hr) && device.get()) {
					hr = activateDevice();
				}
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
			hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, &format.Format, nullptr);
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
			std::stop_callback wakeOnStop(stopToken, [handle = event.get()] { SetEvent(handle); });
			hr = audioClient->SetEventHandle(event.get());
			if (FAILED(hr)) {
				qWarning("Input calibration WASAPI: event registration failed: hr=0x%08lx", hr);
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}

			UINT32 bufferFrameCount = 0;
			hr                      = audioClient->GetBufferSize(&bufferFrameCount);
			if (FAILED(hr) || bufferFrameCount == 0) {
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}

			ComOwner< IAudioRenderClient > renderClient;
			hr = audioClient->GetService(__uuidof(IAudioRenderClient), renderClient.putVoid());
			if (FAILED(hr) || !renderClient.get()) {
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}

			std::size_t cursor = 0;
			const float gain   = std::clamp(target.gain, 0.0f, 2.0f);
			auto queueFrames   = [&](const UINT32 frameCount) -> HRESULT {
                BYTE *bytes    = nullptr;
                HRESULT result = renderClient->GetBuffer(frameCount, &bytes);
                if (FAILED(result)) {
                    return result;
                }
                float *interleaved = reinterpret_cast< float * >(bytes);
                for (UINT32 frame = 0; frame < frameCount; ++frame) {
                    const float sample         = std::clamp(mono48k[cursor++] * gain, -1.0f, 1.0f);
                    interleaved[frame * 2]     = sample;
                    interleaved[frame * 2 + 1] = sample;
                }
                return renderClient->ReleaseBuffer(frameCount, 0);
			};

			const UINT32 initialFrames =
				static_cast< UINT32 >(std::min< std::size_t >(bufferFrameCount, mono48k.size()));
			if (stopToken.stop_requested()) {
				return { Playback::Error::None, usedDefaultFallback };
			}
			hr = queueFrames(initialFrames);
			if (FAILED(hr)) {
				return fail(Playback::Error::BackendFailure, usedDefaultFallback);
			}
			if (stopToken.stop_requested()) {
				audioClient->Reset();
				return { Playback::Error::None, usedDefaultFallback };
			}

	hr = audioClient->Start();
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
				if (cursor == mono48k.size() && padding == 0) {
					break;
				}

				const UINT32 available = bufferFrameCount - padding;
				if (available > 0 && cursor < mono48k.size()) {
					const UINT32 toWrite =
						static_cast< UINT32 >(std::min< std::size_t >(available, mono48k.size() - cursor));
					hr = queueFrames(toWrite);
					if (FAILED(hr)) {
						completion.error = Playback::Error::BackendFailure;
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

CalibrationPlayback::CalibrationPlayback(QObject *parent) : CalibrationPlayback(createPlatformBackend(), parent) {
}

CalibrationPlayback::CalibrationPlayback(std::unique_ptr< Backend > backend, QObject *parent)
	: QObject(parent), m_backend(std::move(backend)) {
}

CalibrationPlayback::~CalibrationPlayback() {
	stop();
}

CalibrationPlayback::StartResult CalibrationPlayback::start(const std::span< const float > mono48k,
															const Target &target) {
	stop();
	if (!m_backend || mono48k.empty() || mono48k.size() > maximumSampleCount
		|| !std::all_of(mono48k.begin(), mono48k.end(), [](const float sample) { return std::isfinite(sample); })) {
		return { Error::InvalidClip, false };
	}

	try {
		auto clip = std::make_shared< SecureClip >(mono48k);
		m_active.store(true, std::memory_order_release);
		m_worker = std::jthread([this, clip = std::move(clip), target](const std::stop_token token) {
			std::atomic_bool readyDelivered{ false };
			std::atomic_bool startedSuccessfully{ false };
			auto ready = [this, &readyDelivered, &startedSuccessfully](const StartResult result) {
				if (readyDelivered.exchange(true, std::memory_order_acq_rel)) {
					return;
				}
				if (result) {
					startedSuccessfully.store(true, std::memory_order_release);
					emit playbackStarted(result.usedDefaultFallback);
				} else {
					emit playbackFailed(static_cast< int >(result.error), false);
				}
			};
			StartResult completion{ Error::BackendFailure, false };
			try {
				completion = m_backend->run(clip->samples, target, token, ready);
			} catch (...) {
				completion = { Error::BackendFailure, false };
			}
			if (!readyDelivered.load(std::memory_order_acquire) && !token.stop_requested()) {
				if (completion.error == Error::None) {
					completion.error = Error::BackendFailure;
				}
				ready(completion);
			}
			m_active.store(false, std::memory_order_release);
			if (startedSuccessfully.load(std::memory_order_acquire) && !token.stop_requested()) {
				if (completion.error == Error::None) {
					emit playbackFinished();
				} else {
					emit playbackFailed(static_cast< int >(completion.error), true);
				}
			}
		});
	} catch (...) {
		m_active.store(false, std::memory_order_release);
		return { Error::ThreadStartFailed, false };
	}
	return { Error::None, false };
}

void CalibrationPlayback::stop() noexcept {
	if (m_worker.joinable()) {
		m_worker.request_stop();
		m_worker.join();
	}
	m_active.store(false, std::memory_order_release);
}

bool CalibrationPlayback::active() const noexcept {
	return m_active.load(std::memory_order_acquire);
}

std::unique_ptr< CalibrationPlayback::Backend > CalibrationPlayback::createPlatformBackend() {
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
	return std::make_unique< WasapiBackend >();
#else
	return std::make_unique< UnsupportedBackend >();
#endif
}

} // namespace Mumble::InputEnhancement
