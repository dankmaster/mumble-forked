// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#if defined(_WIN32)

#	include <cstdio>
#	include <cstdlib>
#	include <cstring>
#	include <windows.h>

#	if !defined(MUMBLE_CLIENT_TEST_TARGET_NAME)
#		define MUMBLE_CLIENT_TEST_TARGET_NAME "Mumble client test"
#	endif

namespace {
constexpr int directLaunchBlockedExitCode = 78;

bool directoryExists(const wchar_t *path) noexcept {
	if (!path || path[0] == L'\0') {
		return false;
	}

	const DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

struct ClientTestRuntimeGuard final {
	ClientTestRuntimeGuard() noexcept {
		const char *platform = std::getenv("QT_QPA_PLATFORM");
		const wchar_t *pluginDirectory = _wgetenv(L"QT_QPA_PLATFORM_PLUGIN_PATH");
		const bool offscreenPlatform = platform && std::strcmp(platform, "offscreen") == 0;

		if (offscreenPlatform && directoryExists(pluginDirectory)) {
			// Keep any later Qt fatal diagnostics in the invoking terminal instead
			// of letting GUI-subsystem helpers create another blocking MessageBox.
			_putenv_s("QT_FORCE_STDERR_LOGGING", "1");
			return;
		}

		std::fprintf(
			stderr,
			"MUMBLE_CLIENT_TEST_DIRECT_LAUNCH_BLOCKED: %s requires the CTest-provisioned Qt runtime.\n"
			"Run it through CTest (or scripts/local/run-shared-webengine-test.ps1) instead of invoking the .exe "
			"directly.\n"
			"Required preflight: QT_QPA_PLATFORM=offscreen and an existing QT_QPA_PLATFORM_PLUGIN_PATH.\n",
			MUMBLE_CLIENT_TEST_TARGET_NAME);
		std::fflush(stderr);
		std::_Exit(directLaunchBlockedExitCode);
	}
};

ClientTestRuntimeGuard clientTestRuntimeGuard;
} // namespace

#endif
