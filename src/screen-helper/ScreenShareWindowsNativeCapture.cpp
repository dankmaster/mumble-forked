// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareWindowsNativeCapture.h"

#include <QtCore/QLibrary>
#include <QtCore/QStringList>

#if defined(Q_OS_WIN)
#	include "win.h"
#	include <d3d11.h>
#	if defined(__has_include)
#		if __has_include(<winrt/Windows.Foundation.Metadata.h>) && __has_include(<winrt/Windows.Graphics.Capture.h>)
#			define MUMBLE_SCREENSHARE_HAS_CPPWINRT_CAPTURE 1
#		endif
#	endif
#	if defined(MUMBLE_SCREENSHARE_HAS_CPPWINRT_CAPTURE)
#		include <winrt/Windows.Foundation.Metadata.h>
#		include <winrt/Windows.Graphics.Capture.h>
#	endif
#endif

namespace {
#if defined(Q_OS_WIN)
bool probeD3D11HardwareDevice(QStringList *warnings) {
	QLibrary d3d11(QStringLiteral("d3d11"));
	if (!d3d11.load()) {
		if (warnings) {
			warnings->append(QStringLiteral("d3d11.dll could not be loaded."));
		}
		return false;
	}

	using D3D11CreateDeviceFn = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
												  const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
												  D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
	auto createDevice = reinterpret_cast< D3D11CreateDeviceFn >(d3d11.resolve("D3D11CreateDevice"));
	if (!createDevice) {
		if (warnings) {
			warnings->append(QStringLiteral("D3D11CreateDevice is unavailable."));
		}
		return false;
	}

	const D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	ID3D11Device *device          = nullptr;
	ID3D11DeviceContext *context = nullptr;
	D3D_FEATURE_LEVEL selectedFeatureLevel{};
	const HRESULT hr = createDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
									D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
									featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &device,
									&selectedFeatureLevel, &context);
	if (context) {
		context->Release();
	}
	if (device) {
		device->Release();
	}

	if (FAILED(hr)) {
		if (warnings) {
			warnings->append(QStringLiteral("A hardware D3D11 device could not be created for native capture."));
		}
		return false;
	}

	return true;
}

#	if defined(MUMBLE_SCREENSHARE_HAS_CPPWINRT_CAPTURE)
void probeWindowsGraphicsCapture(ScreenShareWindowsNativeCapture::Capability *capability) {
	try {
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		} catch (const winrt::hresult_error &error) {
			if (error.code() != RPC_E_CHANGED_MODE) {
				throw;
			}
		}

		namespace Capture = winrt::Windows::Graphics::Capture;
		namespace Metadata = winrt::Windows::Foundation::Metadata;

		capability->graphicsCaptureSupported = Capture::GraphicsCaptureSession::IsSupported();
		capability->freeThreadedFramePoolSupported = Metadata::ApiInformation::IsMethodPresent(
			L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", L"CreateFreeThreaded");
		capability->dirtyRegionMetadataSupported =
			Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.Direct3D11CaptureFrame",
														L"DirtyRegions")
			&& Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession",
														   L"DirtyRegionMode");
	} catch (const winrt::hresult_error &error) {
		capability->warnings.append(
			QStringLiteral("Windows Graphics Capture probe failed: 0x%1")
				.arg(static_cast< quint32 >(error.code()), 8, 16, QLatin1Char('0')));
	} catch (...) {
		capability->warnings.append(QStringLiteral("Windows Graphics Capture probe failed unexpectedly."));
	}
}
#	endif
#endif
} // namespace

QString ScreenShareWindowsNativeCapture::backendToken() {
	return QStringLiteral("windows-graphics-capture-d3d11");
}

ScreenShareWindowsNativeCapture::Capability ScreenShareWindowsNativeCapture::probe() {
	Capability capability;
	capability.backendToken = backendToken();

#if defined(Q_OS_WIN)
	capability.d3d11HardwareDeviceAvailable = probeD3D11HardwareDevice(&capability.warnings);

#	if defined(MUMBLE_SCREENSHARE_HAS_CPPWINRT_CAPTURE)
	probeWindowsGraphicsCapture(&capability);
#	else
	capability.warnings.append(QStringLiteral("C++/WinRT Windows Graphics Capture headers were not available at build time."));
#	endif

	capability.inProcessCapturePipelinePlanned =
		capability.graphicsCaptureSupported && capability.d3d11HardwareDeviceAvailable;
	if (capability.inProcessCapturePipelinePlanned) {
		capability.detail =
			QStringLiteral("Windows Graphics Capture and a hardware D3D11 device are available for the native "
						   "in-process capture pipeline.");
	} else if (!capability.graphicsCaptureSupported) {
		capability.detail =
			QStringLiteral("Windows Graphics Capture is unavailable on this host or OS build.");
	} else {
		capability.detail =
			QStringLiteral("Windows Graphics Capture is available, but no hardware D3D11 device could be opened.");
	}
#else
	capability.detail =
		QStringLiteral("Windows native capture is only available in Windows helper builds.");
#endif

	return capability;
}
