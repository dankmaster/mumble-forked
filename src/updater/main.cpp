// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr DWORD ParentExitTimeoutMsec = 180000;
constexpr DWORD RestartRequired       = 3010;

struct Options {
	DWORD parentPid = 0;
	std::wstring installerPath;
	std::wstring appPath;
	std::wstring workingDirectory;
	std::wstring updaterLogPath;
	std::wstring msiLogPath;
	bool passive = true;
};

std::wstring quoteArgument(const std::wstring &argument) {
	if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		return argument;
	}

	std::wstring quoted = L"\"";
	int backslashes     = 0;
	for (const wchar_t ch : argument) {
		if (ch == L'\\') {
			++backslashes;
		} else if (ch == L'"') {
			quoted.append(static_cast< std::size_t >(backslashes * 2 + 1), L'\\');
			quoted.push_back(ch);
			backslashes = 0;
		} else {
			quoted.append(static_cast< std::size_t >(backslashes), L'\\');
			backslashes = 0;
			quoted.push_back(ch);
		}
	}
	quoted.append(static_cast< std::size_t >(backslashes * 2), L'\\');
	quoted.push_back(L'"');
	return quoted;
}

bool fileExists(const std::wstring &path) {
	if (path.empty()) {
		return false;
	}
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void appendLog(const Options &options, const std::wstring &message) {
	if (options.updaterLogPath.empty()) {
		return;
	}

	std::error_code error;
	const std::filesystem::path logPath(options.updaterLogPath);
	if (logPath.has_parent_path()) {
		std::filesystem::create_directories(logPath.parent_path(), error);
	}

	std::wofstream stream(logPath, std::ios::app);
	if (!stream) {
		return;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);
	stream << L'[' << now.wYear << L'-' << now.wMonth << L'-' << now.wDay << L' ' << now.wHour << L':' << now.wMinute
		   << L':' << now.wSecond << L"] " << message << L'\n';
}

bool parseArguments(int argc, wchar_t **argv, Options &options) {
	for (int i = 1; i < argc; ++i) {
		const std::wstring arg = argv[i];
		const auto nextValue   = [&]() -> std::wstring {
			if (i + 1 >= argc) {
				return {};
			}
			return argv[++i];
		};

		if (arg == L"--parent-pid") {
			options.parentPid = static_cast< DWORD >(std::wcstoul(nextValue().c_str(), nullptr, 10));
		} else if (arg == L"--installer") {
			options.installerPath = nextValue();
		} else if (arg == L"--app") {
			options.appPath = nextValue();
		} else if (arg == L"--working-dir") {
			options.workingDirectory = nextValue();
		} else if (arg == L"--updater-log") {
			options.updaterLogPath = nextValue();
		} else if (arg == L"--msi-log") {
			options.msiLogPath = nextValue();
		} else if (arg == L"--passive") {
			options.passive = true;
		} else if (arg == L"--no-passive") {
			options.passive = false;
		}
	}

	return fileExists(options.installerPath) && fileExists(options.appPath);
}

void waitForParent(const Options &options) {
	if (options.parentPid == 0) {
		return;
	}

	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, options.parentPid);
	if (!process) {
		appendLog(options, L"Parent process was already gone or could not be opened.");
		return;
	}

	appendLog(options, L"Waiting for Mumble to exit.");
	const DWORD waitResult = WaitForSingleObject(process, ParentExitTimeoutMsec);
	CloseHandle(process);

	if (waitResult == WAIT_TIMEOUT) {
		appendLog(options, L"Timed out waiting for Mumble to exit; continuing with installer launch.");
	}
}

DWORD runInstaller(const Options &options) {
	std::wstring parameters = L"/i " + quoteArgument(options.installerPath) + L" /norestart";
	if (options.passive) {
		parameters += L" /passive";
	}
	if (!options.msiLogPath.empty()) {
		parameters += L" /log " + quoteArgument(options.msiLogPath);
	}

	SHELLEXECUTEINFOW executeInfo {};
	executeInfo.cbSize       = sizeof(executeInfo);
	executeInfo.fMask        = SEE_MASK_NOCLOSEPROCESS;
	executeInfo.lpVerb       = L"open";
	executeInfo.lpFile       = L"msiexec.exe";
	executeInfo.lpParameters = parameters.c_str();
	executeInfo.nShow        = SW_SHOWNORMAL;

	appendLog(options, L"Launching Windows Installer.");
	if (!ShellExecuteExW(&executeInfo)) {
		const DWORD error = GetLastError();
		appendLog(options, L"Failed to launch msiexec.exe. Error " + std::to_wstring(error) + L'.');
		return error == 0 ? 1 : error;
	}

	if (executeInfo.hProcess) {
		WaitForSingleObject(executeInfo.hProcess, INFINITE);
		DWORD exitCode = 0;
		if (!GetExitCodeProcess(executeInfo.hProcess, &exitCode)) {
			exitCode = GetLastError();
		}
		CloseHandle(executeInfo.hProcess);
		appendLog(options, L"Windows Installer exited with code " + std::to_wstring(exitCode) + L'.');
		return exitCode;
	}

	return 0;
}

bool relaunchMumble(const Options &options) {
	if (!fileExists(options.appPath)) {
		appendLog(options, L"Mumble executable is missing after installation.");
		return false;
	}

	SHELLEXECUTEINFOW executeInfo {};
	executeInfo.cbSize      = sizeof(executeInfo);
	executeInfo.lpVerb      = L"open";
	executeInfo.lpFile      = options.appPath.c_str();
	executeInfo.lpDirectory = options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str();
	executeInfo.nShow       = SW_SHOWNORMAL;

	appendLog(options, L"Restarting Mumble.");
	if (!ShellExecuteExW(&executeInfo)) {
		const DWORD error = GetLastError();
		appendLog(options, L"Failed to restart Mumble. Error " + std::to_wstring(error) + L'.');
		return false;
	}
	return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	int argc        = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) {
		return 1;
	}

	Options options;
	const bool valid = parseArguments(argc, argv, options);
	LocalFree(argv);

	if (!valid) {
		appendLog(options, L"Missing or invalid updater arguments.");
		return 2;
	}

	appendLog(options, L"MumbleUpdater started.");
	waitForParent(options);

	const DWORD installerExitCode = runInstaller(options);
	if (installerExitCode == 0 || installerExitCode == RestartRequired) {
		Sleep(800);
		relaunchMumble(options);
	}

	appendLog(options, L"MumbleUpdater finished.");
	return static_cast< int >(installerExitCode);
}
