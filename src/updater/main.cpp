// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UpdateHealth.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <wincrypt.h>
#include <zlib.h>

#include <nlohmann/json.hpp>

namespace {

constexpr DWORD ParentExitTimeoutMsec                     = 180000;
constexpr DWORD RestartRequired                           = 3010;
constexpr DWORD InstallerUserExit                         = 1602;
constexpr std::uint16_t ZipMethodStore                    = 0;
constexpr std::uint16_t ZipMethodDeflate                  = 8;
constexpr std::size_t ZipReadBufferSize                   = 1024 * 1024;
constexpr std::uint32_t ZipLocalFileHeaderSignature       = 0x04034b50;
constexpr std::uint32_t ZipCentralDirectorySignature      = 0x02014b50;
constexpr std::uint32_t ZipEndOfCentralDirectorySignature = 0x06054b50;
constexpr std::uint32_t Zip64Marker32                     = 0xffffffffu;
constexpr std::uint16_t Zip64Marker16                     = 0xffffu;

using json = nlohmann::json;

bool updateSucceeded(const DWORD exitCode) {
	return exitCode == 0 || exitCode == RestartRequired;
}

bool updateCancelled(const DWORD exitCode) {
	return exitCode == ERROR_CANCELLED || exitCode == InstallerUserExit;
}

std::string currentBootSessionIdentity() {
	// SystemBootEnvironmentInformation exposes the per-boot GUID used by the
	// Windows kernel. Resolve NtQuerySystemInformation dynamically so the updater
	// remains self-contained and can fail closed when the information is absent.
	struct BootEnvironmentInformation {
		GUID bootIdentifier;
		ULONG firmwareType;
		ULONGLONG bootFlags;
	};
	using QuerySystemInformation = LONG(WINAPI *)(ULONG, PVOID, ULONG, PULONG);
	constexpr ULONG SystemBootEnvironmentInformation = 90;

	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	const auto query = ntdll ? reinterpret_cast< QuerySystemInformation >(
								 GetProcAddress(ntdll, "NtQuerySystemInformation"))
						 : nullptr;
	if (!query) {
		return {};
	}
	BootEnvironmentInformation information{};
	ULONG returnedLength = 0;
	const LONG status = query(SystemBootEnvironmentInformation, &information,
							  static_cast< ULONG >(sizeof(information)), &returnedLength);
	if (status < 0 || returnedLength < sizeof(GUID)) {
		return {};
	}

	const GUID &id = information.bootIdentifier;
	std::ostringstream stream;
	stream << std::hex << std::nouppercase << std::setfill('0')
		   << std::setw(8) << id.Data1 << std::setw(4) << id.Data2 << std::setw(4) << id.Data3;
	for (const unsigned char byte : id.Data4) {
		stream << std::setw(2) << static_cast< unsigned int >(byte);
	}
	return stream.str();
}

struct Options {
	DWORD parentPid = 0;
	std::wstring installerPath;
	std::wstring recoveryInstallerPath;
	std::wstring packagePath;
	std::wstring appPath;
	std::wstring workingDirectory;
	std::wstring updaterLogPath;
	std::wstring msiLogPath;
	std::wstring uiTheme;
	std::wstring packageSha256;
	std::wstring installerSha256;
	std::wstring recoveryInstallerSha256;
	std::wstring candidateExecutableSha256;
	bool passive       = true;
	bool noRelaunch    = false;
	bool noUi          = false;
	bool prepareOnly   = false;
	bool recoverOnly   = false;
#ifdef MUMBLE_UPDATER_TEST_HOOKS
	bool testCrashAfterJournal       = false;
	bool testCrashAfterFirstMutation = false;
	bool testSkipRecoveryWatchdog    = false;
#endif
};

std::wstring recoveryUpdaterArguments(const Options &options);

bool rollbackPendingWindowsInstaller(const Options &options,
									 Mumble::UpdateHealth::PendingUpdate pending);
bool rollbackPendingUpdate(const Options &options);
std::optional< Mumble::UpdateHealth::PendingUpdate > armWindowsInstallerHealth(const Options &options);

void postUiStatus(const std::wstring &message);
void postUiProgress(int percent, bool indeterminate);

std::string utf8FromWide(const std::wstring &value) {
	if (value.empty()) {
		return {};
	}

	const int byteCount =
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast< int >(value.size()), nullptr, 0, nullptr, nullptr);
	if (byteCount <= 0) {
		return {};
	}

	std::string result(static_cast< std::size_t >(byteCount), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast< int >(value.size()), result.data(), byteCount, nullptr,
						nullptr);
	return result;
}

std::wstring quoteArgument(const std::wstring &argument) {
	if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
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

std::wstring shellCompatiblePath(const std::wstring &path) {
	constexpr std::wstring_view ExtendedUncPrefix = L"\\\\?\\UNC\\";
	constexpr std::wstring_view ExtendedDosPrefix = L"\\\\?\\";
	if (path.size() >= ExtendedUncPrefix.size()
		&& std::equal(ExtendedUncPrefix.begin(), ExtendedUncPrefix.end(), path.begin(),
					  [](const wchar_t lhs, const wchar_t rhs) {
						  return std::towlower(lhs) == std::towlower(rhs);
					  })) {
		return L"\\\\" + path.substr(ExtendedUncPrefix.size());
	}
	if (path.size() >= ExtendedDosPrefix.size()
		&& std::equal(ExtendedDosPrefix.begin(), ExtendedDosPrefix.end(), path.begin())) {
		return path.substr(ExtendedDosPrefix.size());
	}
	return path;
}

bool fileExists(const std::wstring &path) {
	if (path.empty()) {
		return false;
	}
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::filesystem::path parentPath(const std::wstring &path) {
	if (path.empty()) {
		return {};
	}
	return std::filesystem::path(path).parent_path();
}

std::filesystem::path packageWorkRoot(const Options &options) {
	const std::filesystem::path logParent = parentPath(options.updaterLogPath);
	if (!logParent.empty()) {
		return logParent;
	}
	if (!options.workingDirectory.empty()) {
		return std::filesystem::path(options.workingDirectory);
	}
	return std::filesystem::temp_directory_path();
}

bool normalizeSha256Argument(std::wstring &value) {
	value.erase(std::remove_if(value.begin(), value.end(), [](const wchar_t ch) { return std::iswspace(ch) != 0; }),
				value.end());
	std::transform(value.begin(), value.end(), value.begin(),
				   [](const wchar_t ch) { return static_cast< wchar_t >(std::towlower(ch)); });
	return value.size() == 64
		   && std::all_of(value.begin(), value.end(), [](const wchar_t ch) { return std::iswxdigit(ch) != 0; });
}

void appendLog(const Options &options, const std::wstring &message) {
	postUiStatus(message);
	if (options.updaterLogPath.empty()) {
		return;
	}

	std::error_code error;
	const std::filesystem::path logPath(options.updaterLogPath);
	if (logPath.has_parent_path()) {
		std::filesystem::create_directories(logPath.parent_path(), error);
	}

	SYSTEMTIME now;
	GetLocalTime(&now);
	std::wostringstream line;
	line << L'[' << std::setfill(L'0') << std::setw(4) << now.wYear << L'-' << std::setw(2) << now.wMonth << L'-'
		 << std::setw(2) << now.wDay << L' ' << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
		 << std::setw(2) << now.wSecond << L"] " << message;

	std::string bytes = utf8FromWide(line.str());
	bytes.push_back('\n');

	HANDLE file = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 8; ++attempt) {
		file = CreateFileW(logPath.wstring().c_str(), FILE_APPEND_DATA,
						   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
						   FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) {
			break;
		}
		Sleep(25);
	}
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	DWORD written = 0;
	WriteFile(file, bytes.data(), static_cast< DWORD >(bytes.size()), &written, nullptr);
	CloseHandle(file);
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
		} else if (arg == L"--recovery-installer") {
			options.recoveryInstallerPath = nextValue();
		} else if (arg == L"--package") {
			options.packagePath = nextValue();
		} else if (arg == L"--app") {
			options.appPath = nextValue();
		} else if (arg == L"--working-dir") {
			options.workingDirectory = nextValue();
		} else if (arg == L"--updater-log") {
			options.updaterLogPath = nextValue();
		} else if (arg == L"--msi-log") {
			options.msiLogPath = nextValue();
		} else if (arg == L"--ui-theme") {
			options.uiTheme = nextValue();
		} else if (arg == L"--package-sha256") {
			options.packageSha256 = nextValue();
		} else if (arg == L"--installer-sha256") {
			options.installerSha256 = nextValue();
		} else if (arg == L"--recovery-installer-sha256") {
			options.recoveryInstallerSha256 = nextValue();
		} else if (arg == L"--candidate-executable-sha256") {
			options.candidateExecutableSha256 = nextValue();
		} else if (arg == L"--passive") {
			options.passive = true;
		} else if (arg == L"--no-passive") {
			options.passive = false;
		} else if (arg == L"--no-relaunch") {
			options.noRelaunch = true;
		} else if (arg == L"--no-ui") {
			options.noUi = true;
		} else if (arg == L"--prepare") {
			options.prepareOnly = true;
		} else if (arg == L"--recover") {
			options.recoverOnly = true;
#ifdef MUMBLE_UPDATER_TEST_HOOKS
		} else if (arg == L"--test-crash-after-journal") {
			options.testCrashAfterJournal = true;
		} else if (arg == L"--test-crash-after-first-mutation") {
			options.testCrashAfterFirstMutation = true;
		} else if (arg == L"--test-skip-recovery-watchdog") {
			options.testSkipRecoveryWatchdog = true;
#endif
		}
	}

	const bool hasInstaller = fileExists(options.installerPath);
	const bool hasPackage   = fileExists(options.packagePath);
	const bool hasRecoveryInstaller = fileExists(options.recoveryInstallerPath);
	if (!hasInstaller) {
		options.installerPath.clear();
	}
	if (!hasPackage) {
		options.packagePath.clear();
	}
	if (!hasRecoveryInstaller) {
		options.recoveryInstallerPath.clear();
	}
	if (hasPackage && !normalizeSha256Argument(options.packageSha256)) {
		return false;
	}
	if (hasInstaller && !normalizeSha256Argument(options.installerSha256)) {
		return false;
	}
	if (hasRecoveryInstaller && !normalizeSha256Argument(options.recoveryInstallerSha256)) {
		return false;
	}
	if (!options.candidateExecutableSha256.empty()
		&& !normalizeSha256Argument(options.candidateExecutableSha256)) {
		return false;
	}
	if (hasRecoveryInstaller != !options.recoveryInstallerSha256.empty()) {
		return false;
	}
	if (!options.recoverOnly && hasInstaller && hasRecoveryInstaller
		&& options.candidateExecutableSha256.empty()) {
		return false;
	}
	return !options.appPath.empty()
		   && (options.recoverOnly || (fileExists(options.appPath) && (hasInstaller || hasPackage)));
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
	const std::wstring installerPath = shellCompatiblePath(options.installerPath);
	if (installerPath != options.installerPath) {
		appendLog(options, L"Normalized the verified MSI path for Windows Installer compatibility.");
	}
	std::wstring parameters = L"/i " + quoteArgument(installerPath) + L" /norestart";
	if (options.passive) {
		parameters += L" /passive";
	}
	if (!options.msiLogPath.empty()) {
		parameters += L" /log " + quoteArgument(shellCompatiblePath(options.msiLogPath));
	}

	std::wstring systemDirectory(MAX_PATH, L'\0');
	UINT systemLength = GetSystemDirectoryW(systemDirectory.data(), static_cast< UINT >(systemDirectory.size()));
	if (systemLength >= systemDirectory.size()) {
		systemDirectory.resize(static_cast< std::size_t >(systemLength) + 1);
		systemLength = GetSystemDirectoryW(systemDirectory.data(), static_cast< UINT >(systemDirectory.size()));
	}
	if (systemLength == 0 || systemLength >= systemDirectory.size()) {
		appendLog(options, L"Unable to resolve the trusted Windows system directory for msiexec.exe.");
		return ERROR_FILE_NOT_FOUND;
	}
	systemDirectory.resize(systemLength);
	const std::filesystem::path msiexecPath = std::filesystem::path(systemDirectory) / L"msiexec.exe";
	if (!fileExists(msiexecPath.wstring())) {
		appendLog(options, L"The trusted system msiexec.exe is missing.");
		return ERROR_FILE_NOT_FOUND;
	}
	const std::wstring msiexecExecutable = msiexecPath.wstring();

	SHELLEXECUTEINFOW executeInfo{};
	executeInfo.cbSize       = sizeof(executeInfo);
	executeInfo.fMask        = SEE_MASK_NOCLOSEPROCESS;
	executeInfo.lpVerb       = L"open";
	executeInfo.lpFile       = msiexecExecutable.c_str();
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

	appendLog(options, L"Windows Installer launch returned no process handle; refusing an unobserved update.");
	return ERROR_INVALID_HANDLE;
}

bool directoryWritable(const Options &options, const std::filesystem::path &directory) {
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		appendLog(options, L"Unable to create directory for write test: " + directory.wstring());
		return false;
	}

	const std::filesystem::path testPath =
		directory / (L".mumble-updater-write-test-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
	HANDLE file = CreateFileW(testPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
							  FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		appendLog(options, L"Write test failed for " + directory.wstring() + L". Error "
							   + std::to_wstring(GetLastError()) + L'.');
		return false;
	}

	CloseHandle(file);
	DeleteFileW(testPath.wstring().c_str());
	return true;
}

bool processIsElevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return true;
	}
	TOKEN_ELEVATION elevation{};
	DWORD returned = 0;
	const bool queried = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned) != FALSE;
	CloseHandle(token);
	// Fail closed if the token cannot be classified.
	return !queried || elevation.TokenIsElevated != 0;
}

std::wstring currentExecutablePath() {
	std::wstring path(MAX_PATH, L'\0');
	DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast< DWORD >(path.size()));
	while (length == path.size()) {
		path.resize(path.size() * 2);
		length = GetModuleFileNameW(nullptr, path.data(), static_cast< DWORD >(path.size()));
	}
	path.resize(length);
	return path;
}

bool writePackageApplyScript(const Options &options, const std::filesystem::path &scriptPath) {
	std::error_code error;
	std::filesystem::create_directories(scriptPath.parent_path(), error);
	if (error) {
		appendLog(options,
				  L"Unable to create package script directory. Error " + std::to_wstring(error.value()) + L'.');
		return false;
	}

	std::ofstream stream(scriptPath, std::ios::binary | std::ios::trunc);
	if (!stream) {
		appendLog(options, L"Unable to write package apply script.");
		return false;
	}

	stream << R"PS(param(
	[Parameter(Mandatory = $true)]
	[string] $PackagePath,
	[Parameter(Mandatory = $true)]
	[string] $AppPath,
	[Parameter(Mandatory = $true)]
	[string] $UpdaterLogPath,
	[switch] $NoRelaunch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-UpdaterLog {
	param([string] $Message)
	if ([string]::IsNullOrWhiteSpace($UpdaterLogPath)) {
		return
	}
	$logDir = Split-Path -Parent $UpdaterLogPath
	if (-not [string]::IsNullOrWhiteSpace($logDir)) {
		New-Item -ItemType Directory -Force -Path $logDir | Out-Null
	}
	$line = ("[{0}] {1}" -f (Get-Date).ToString('s'), $Message) + [System.Environment]::NewLine
	$bytes = [System.Text.Encoding]::UTF8.GetBytes($line)
	for ($attempt = 0; $attempt -lt 8; $attempt++) {
		try {
			$share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
			$stream = [System.IO.File]::Open($UpdaterLogPath, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, $share)
			try {
				$stream.Write($bytes, 0, $bytes.Length)
			} finally {
				$stream.Dispose()
			}
			return
		} catch [System.IO.IOException] {
			if ($attempt -ge 7) {
				throw
			}
			Start-Sleep -Milliseconds 25
		}
	}
}

function Assert-SafePackagePath {
	param([Parameter(Mandatory = $true)][string] $RelativePath)
	if ([string]::IsNullOrWhiteSpace($RelativePath)) {
		throw 'Package manifest contains an empty path.'
	}
	if ([System.IO.Path]::IsPathRooted($RelativePath)) {
		throw "Package manifest contains a rooted path: $RelativePath"
	}
	if (($RelativePath -split '/') -contains '..') {
		throw "Package manifest contains a parent traversal path: $RelativePath"
	}
}

function Resolve-UnderRoot {
	param(
		[Parameter(Mandatory = $true)][string] $Root,
		[Parameter(Mandatory = $true)][string] $RelativePath
	)
	Assert-SafePackagePath -RelativePath $RelativePath
	$rootFull = [System.IO.Path]::GetFullPath($Root)
	if (-not $rootFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
		$rootFull += [System.IO.Path]::DirectorySeparatorChar
	}
	$nativeRelative = $RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar
	$full = [System.IO.Path]::GetFullPath((Join-Path $Root $nativeRelative))
	if (-not $full.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Package path escapes its root: $RelativePath"
	}
	return $full
}

function Get-Sha256 {
	param([Parameter(Mandatory = $true)][string] $Path)
	$stream = [System.IO.File]::OpenRead($Path)
	try {
		$sha256 = [System.Security.Cryptography.SHA256]::Create()
		try {
			return (($sha256.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) -join '')
		} finally {
			$sha256.Dispose()
		}
	} finally {
		$stream.Dispose()
	}
}

$updateRoot = Split-Path -Parent $UpdaterLogPath
if ([string]::IsNullOrWhiteSpace($updateRoot)) {
	$updateRoot = [System.IO.Path]::GetTempPath()
}
$extractRoot = Join-Path $updateRoot ('package-extract-' + [System.Guid]::NewGuid().ToString('N'))
$backupRoot = Join-Path $updateRoot ('package-backup-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [System.Guid]::NewGuid().ToString('N'))
$backups = New-Object System.Collections.Generic.List[object]

try {
	Write-UpdaterLog "Extracting update package: $PackagePath"
	New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	[System.IO.Compression.ZipFile]::ExtractToDirectory($PackagePath, $extractRoot)

	$manifestPath = Join-Path $extractRoot 'manifest.json'
	$payloadRoot = Join-Path $extractRoot 'payload'
	if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
		throw 'Update package is missing manifest.json.'
	}
	if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container)) {
		throw 'Update package is missing payload/.'
	}

	$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
	if ($manifest.format -ne 'mumble-update-v1') {
		throw "Unsupported update package format: $($manifest.format)"
	}
	if ($manifest.applyMode -ne 'replace-staged-payload') {
		throw "Unsupported update package apply mode: $($manifest.applyMode)"
	}
	$minUpdaterVersion = [int] $manifest.minUpdaterVersion
	if ($minUpdaterVersion -gt 4) {
		throw "Update package requires updater version $minUpdaterVersion."
	}

	$files = @($manifest.files)
	if ($files.Count -eq 0) {
		throw 'Update package manifest does not list any files.'
	}
	$packagePaths = @($files | ForEach-Object { ([string] $_.path).Replace('\', '/') })
	if ($packagePaths -notcontains 'mumble.exe') {
		throw 'Update package is missing mumble.exe.'
	}
	if ($packagePaths -notcontains 'mumble-updater.exe') {
		throw 'Update package is missing mumble-updater.exe.'
	}

	$totalProgressSteps = [Math]::Max(1, ($files.Count * 3) + 3)
	$progressStep = 0
	function Write-ProgressLog {
		param([string] $Message)
		if ($script:progressStep -lt $script:totalProgressSteps) {
			$script:progressStep++
		}
		Write-UpdaterLog ("Progress {0}/{1}: {2}" -f $script:progressStep, $script:totalProgressSteps, $Message)
	}
	Write-ProgressLog 'Validated update package manifest'

	foreach ($file in $files) {
		Write-ProgressLog "Verifying package file $($file.path)"
		$source = Resolve-UnderRoot -Root $payloadRoot -RelativePath $file.path
		if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
			throw "Update package payload is missing $($file.path)."
		}
		$item = Get-Item -LiteralPath $source
		if ([int64] $file.size -ne [int64] $item.Length) {
			throw "Update package size mismatch for $($file.path)."
		}
		$hash = Get-Sha256 -Path $source
		if ($hash -ne ([string] $file.sha256).ToLowerInvariant()) {
			throw "Update package SHA256 mismatch for $($file.path)."
		}
	}

	$appDir = Split-Path -Parent $AppPath
	if ([string]::IsNullOrWhiteSpace($appDir)) {
		throw 'Unable to resolve the Mumble app directory.'
	}

	Write-ProgressLog 'Preparing backup directory'
	New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
	try {
		foreach ($file in $files) {
			Write-ProgressLog "Installing file $($file.path)"
			$source = Resolve-UnderRoot -Root $payloadRoot -RelativePath $file.path
			$target = Resolve-UnderRoot -Root $appDir -RelativePath $file.path
			$targetDir = Split-Path -Parent $target
			if (-not [string]::IsNullOrWhiteSpace($targetDir)) {
				New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
			}

			$backup = Resolve-UnderRoot -Root $backupRoot -RelativePath $file.path
			$backupDir = Split-Path -Parent $backup
			if (-not [string]::IsNullOrWhiteSpace($backupDir)) {
				New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
			}

			$existed = Test-Path -LiteralPath $target -PathType Leaf
			if ($existed) {
				Copy-Item -LiteralPath $target -Destination $backup -Force
			}
			$backups.Add([pscustomobject]@{
				Target = $target
				Backup = $backup
				Existed = $existed
			})

			Copy-Item -LiteralPath $source -Destination $target -Force
		}

		foreach ($file in $files) {
			Write-ProgressLog "Verifying installed file $($file.path)"
			$target = Resolve-UnderRoot -Root $appDir -RelativePath $file.path
			if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
				throw "Updated file is missing after copy: $($file.path)."
			}
			$item = Get-Item -LiteralPath $target
			if ([int64] $file.size -ne [int64] $item.Length) {
				throw "Updated file size mismatch after copy: $($file.path)."
			}
			$hash = Get-Sha256 -Path $target
			if ($hash -ne ([string] $file.sha256).ToLowerInvariant()) {
				throw "Updated file SHA256 mismatch after copy: $($file.path)."
			}
		}
	} catch {
		Write-UpdaterLog "Package apply failed; restoring backup. $($_.Exception.Message)"
		for ($i = $backups.Count - 1; $i -ge 0; $i--) {
			$entry = $backups[$i]
			if ($entry.Existed) {
				$restoreDir = Split-Path -Parent $entry.Target
				if (-not [string]::IsNullOrWhiteSpace($restoreDir)) {
					New-Item -ItemType Directory -Force -Path $restoreDir | Out-Null
				}
				Copy-Item -LiteralPath $entry.Backup -Destination $entry.Target -Force
			} elseif (Test-Path -LiteralPath $entry.Target -PathType Leaf) {
				Remove-Item -LiteralPath $entry.Target -Force
			}
		}
		throw
	}

	Write-ProgressLog 'Update package applied successfully'
	Write-UpdaterLog "Update package applied successfully."
	if (-not $NoRelaunch) {
		Start-Process -FilePath $AppPath -WorkingDirectory $appDir
	}
} catch {
	Write-UpdaterLog "Package apply failed. $($_.Exception.Message)"
	throw
} finally {
	if (Test-Path -LiteralPath $extractRoot) {
		Remove-Item -LiteralPath $extractRoot -Recurse -Force
	}
}
)PS";

	return static_cast< bool >(stream);
}

DWORD runPowerShellPackageApply(const Options &options, const std::filesystem::path &scriptPath) {
	std::wstring parameters = L"-NoProfile -ExecutionPolicy Bypass -File " + quoteArgument(scriptPath.wstring());
	parameters += L" -PackagePath " + quoteArgument(options.packagePath);
	parameters += L" -AppPath " + quoteArgument(options.appPath);
	parameters += L" -UpdaterLogPath " + quoteArgument(options.updaterLogPath);
	if (options.noRelaunch) {
		parameters += L" -NoRelaunch";
	}

	SHELLEXECUTEINFOW executeInfo{};
	executeInfo.cbSize       = sizeof(executeInfo);
	executeInfo.fMask        = SEE_MASK_NOCLOSEPROCESS;
	executeInfo.lpVerb       = L"open";
	executeInfo.lpFile       = L"powershell.exe";
	executeInfo.lpParameters = parameters.c_str();
	executeInfo.lpDirectory  = options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str();
	executeInfo.nShow        = SW_HIDE;

	appendLog(options, L"Launching PowerShell package apply worker.");
	if (!ShellExecuteExW(&executeInfo)) {
		const DWORD error = GetLastError();
		appendLog(options, L"Failed to launch PowerShell package worker. Error " + std::to_wstring(error) + L'.');
		return error == 0 ? 1 : error;
	}

	if (executeInfo.hProcess) {
		WaitForSingleObject(executeInfo.hProcess, INFINITE);
		DWORD exitCode = 0;
		if (!GetExitCodeProcess(executeInfo.hProcess, &exitCode)) {
			exitCode = GetLastError();
		}
		CloseHandle(executeInfo.hProcess);
		appendLog(options, L"Package apply worker exited with code " + std::to_wstring(exitCode) + L'.');
		return exitCode;
	}

	return 0;
}

std::wstring wideFromUtf8(const std::string &value) {
	if (value.empty()) {
		return {};
	}

	int length =
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast< int >(value.size()), nullptr, 0);
	if (length <= 0) {
		length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast< int >(value.size()), nullptr, 0);
	}
	if (length <= 0) {
		return {};
	}

	std::wstring result(static_cast< std::size_t >(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast< int >(value.size()), result.data(), length);
	return result;
}

std::string lowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
				   [](const unsigned char ch) { return static_cast< char >(std::tolower(ch)); });
	return value;
}

std::string lowerSha256(const std::wstring &value) {
	std::string result = utf8FromWide(value);
	result.erase(std::remove_if(result.begin(), result.end(), [](const unsigned char ch) { return std::isspace(ch); }),
				 result.end());
	return lowerAscii(result);
}

std::wstring lowerWide(std::wstring value) {
	std::transform(value.begin(), value.end(), value.begin(),
				   [](const wchar_t ch) { return static_cast< wchar_t >(std::towlower(ch)); });
	return value;
}

bool pathStartsWithRoot(const std::filesystem::path &path, const std::filesystem::path &root) {
	std::wstring pathText = lowerWide(std::filesystem::absolute(path).lexically_normal().wstring());
	std::wstring rootText = lowerWide(std::filesystem::absolute(root).lexically_normal().wstring());
	if (!rootText.empty() && rootText.back() != L'\\' && rootText.back() != L'/') {
		rootText.push_back(L'\\');
	}
	if (!pathText.empty() && pathText.back() != L'\\' && pathText.back() != L'/') {
		pathText.push_back(L'\\');
	}
	return pathText.rfind(rootText, 0) == 0;
}

bool isSafePackageRelativePath(const std::string &relativePath) {
	if (relativePath.empty() || relativePath.front() == '/' || relativePath.front() == '\\') {
		return false;
	}
	// Package manifests use one canonical separator. Accepting a backslash here
	// would let a value such as "directory\\..\\outside" bypass the component
	// checks below and become a traversal only when std::filesystem parses it on
	// Windows.
	if (relativePath.find(':') != std::string::npos || relativePath.find('\\') != std::string::npos
		|| relativePath.back() == '/') {
		return false;
	}

	std::size_t start = 0;
	while (start <= relativePath.size()) {
		const std::size_t end  = relativePath.find('/', start);
		const std::string part = relativePath.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (part.empty() || part == "." || part == ".." || part.back() == ' ' || part.back() == '.') {
			return false;
		}
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
	return true;
}

std::filesystem::path resolvePackagePathUnderRoot(const std::filesystem::path &root, const std::string &relativePath) {
	if (!isSafePackageRelativePath(relativePath)) {
		throw std::runtime_error("Unsafe package path: " + relativePath);
	}

	const DWORD rootAttributes = GetFileAttributesW(root.wstring().c_str());
	if (rootAttributes != INVALID_FILE_ATTRIBUTES && (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
		throw std::runtime_error("Package root is a reparse point: " + relativePath);
	}

	std::filesystem::path result = root;
	std::size_t start            = 0;
	while (start <= relativePath.size()) {
		const std::size_t end  = relativePath.find('/', start);
		const std::string part = relativePath.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!part.empty() && part != ".") {
			result /= wideFromUtf8(part);
			const DWORD attributes = GetFileAttributesW(result.wstring().c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
				throw std::runtime_error("Package path traverses a reparse point: " + relativePath);
			}
		}
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}

	if (!pathStartsWithRoot(result, root)) {
		throw std::runtime_error("Package path escapes its root: " + relativePath);
	}
	return result;
}

std::wstring pathWithSuffix(const std::filesystem::path &path, const std::wstring &suffix) {
	return path.wstring() + suffix;
}

std::string hexFromBytes(const BYTE *bytes, const DWORD length) {
	std::ostringstream stream;
	stream << std::hex << std::setfill('0');
	for (DWORD index = 0; index < length; ++index) {
		stream << std::setw(2) << static_cast< int >(bytes[index]);
	}
	return stream.str();
}

class Sha256Hasher {
public:
	Sha256Hasher() {
		if (!CryptAcquireContextW(&m_provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
			throw std::runtime_error("Unable to acquire SHA256 crypto provider.");
		}
		if (!CryptCreateHash(m_provider, CALG_SHA_256, 0, 0, &m_hash)) {
			CryptReleaseContext(m_provider, 0);
			m_provider = 0;
			throw std::runtime_error("Unable to create SHA256 hash.");
		}
	}

	~Sha256Hasher() {
		if (m_hash) {
			CryptDestroyHash(m_hash);
		}
		if (m_provider) {
			CryptReleaseContext(m_provider, 0);
		}
	}

	void add(const char *data, const std::size_t size) {
		if (size == 0) {
			return;
		}
		if (size > std::numeric_limits< DWORD >::max()) {
			throw std::runtime_error("SHA256 input chunk is too large.");
		}
		if (!CryptHashData(m_hash, reinterpret_cast< const BYTE * >(data), static_cast< DWORD >(size), 0)) {
			throw std::runtime_error("Unable to update SHA256 hash.");
		}
	}

	std::string finishHex() {
		BYTE hash[32]{};
		DWORD hashLength = static_cast< DWORD >(sizeof(hash));
		if (!CryptGetHashParam(m_hash, HP_HASHVAL, hash, &hashLength, 0)) {
			throw std::runtime_error("Unable to finish SHA256 hash.");
		}
		return hexFromBytes(hash, hashLength);
	}

private:
	HCRYPTPROV m_provider = 0;
	HCRYPTHASH m_hash     = 0;
};

std::string fileSha256(const std::filesystem::path &path) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		throw std::runtime_error("Unable to open file for SHA256: " + utf8FromWide(path.wstring()));
	}

	Sha256Hasher hasher;
	std::vector< char > buffer(ZipReadBufferSize);
	while (stream) {
		stream.read(buffer.data(), static_cast< std::streamsize >(buffer.size()));
		const std::streamsize read = stream.gcount();
		if (read > 0) {
			hasher.add(buffer.data(), static_cast< std::size_t >(read));
		}
	}
	return hasher.finishHex();
}

class VerifiedArtifactFile final {
public:
	VerifiedArtifactFile(const std::filesystem::path &path, const std::string &expectedSha256,
					 const std::string &artifactName) {
		m_file = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
						 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (m_file == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("Unable to lock the " + artifactName + " for verification.");
		}
		FILE_ATTRIBUTE_TAG_INFO attributes{};
		if (!GetFileInformationByHandleEx(m_file, FileAttributeTagInfo, &attributes, sizeof(attributes))
			|| (attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
			fail("Update artifact must be a regular non-reparse file.");
		}

		Sha256Hasher hasher;
		std::vector< char > buffer(ZipReadBufferSize);
		for (;;) {
			DWORD read = 0;
			if (!ReadFile(m_file, buffer.data(), static_cast< DWORD >(buffer.size()), &read, nullptr)) {
				fail("Unable to hash the locked update artifact.");
			}
			if (read == 0) {
				break;
			}
			hasher.add(buffer.data(), read);
		}
		if (hasher.finishHex() != expectedSha256) {
			fail("Update artifact SHA256 does not match its mandatory digest.");
		}
		LARGE_INTEGER start{};
		if (!SetFilePointerEx(m_file, start, nullptr, FILE_BEGIN)) {
			fail("Unable to rewind the verified update artifact.");
		}

		std::wstring finalPath(1024, L'\0');
		DWORD length = GetFinalPathNameByHandleW(m_file, finalPath.data(), static_cast< DWORD >(finalPath.size()),
										 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		if (length >= finalPath.size()) {
			finalPath.resize(static_cast< std::size_t >(length) + 1);
			length = GetFinalPathNameByHandleW(m_file, finalPath.data(), static_cast< DWORD >(finalPath.size()),
										 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		}
		if (length == 0 || length >= finalPath.size()) {
			fail("Unable to resolve the verified update artifact's final path.");
		}
		finalPath.resize(length);
		m_finalPath = std::filesystem::path(finalPath);
	}

	~VerifiedArtifactFile() {
		if (m_file != INVALID_HANDLE_VALUE) {
			CloseHandle(m_file);
		}
	}

	VerifiedArtifactFile(const VerifiedArtifactFile &)            = delete;
	VerifiedArtifactFile &operator=(const VerifiedArtifactFile &) = delete;

	const std::filesystem::path &finalPath() const noexcept {
		return m_finalPath;
	}

private:
	[[noreturn]] void fail(const char *message) {
		CloseHandle(m_file);
		m_file = INVALID_HANDLE_VALUE;
		throw std::runtime_error(message);
	}

	HANDLE m_file = INVALID_HANDLE_VALUE;
	std::filesystem::path m_finalPath;
};

class InstallationMutex final {
public:
	explicit InstallationMutex(const std::filesystem::path &appPath) {
		const std::filesystem::path requestedDirectory = appPath.parent_path();
		m_directory = CreateFileW(requestedDirectory.wstring().c_str(), FILE_READ_ATTRIBUTES,
							  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
							  FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (m_directory == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("Unable to lease the physical installation directory.");
		}
		std::wstring finalDirectory(1024, L'\0');
		DWORD length = GetFinalPathNameByHandleW(m_directory, finalDirectory.data(),
										 static_cast< DWORD >(finalDirectory.size()),
										 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		if (length >= finalDirectory.size()) {
			finalDirectory.resize(static_cast< std::size_t >(length) + 1);
			length = GetFinalPathNameByHandleW(m_directory, finalDirectory.data(),
											 static_cast< DWORD >(finalDirectory.size()),
											 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		}
		if (length == 0 || length >= finalDirectory.size()) {
			fail("Unable to resolve the physical installation directory.");
		}
		finalDirectory.resize(length);
		m_finalDirectory = std::filesystem::path(finalDirectory);
		CloseHandle(m_directory);
		m_directory = INVALID_HANDLE_VALUE;

		const std::wstring name = L"Global\\MumbleUpdater-v3-"
							  + wideFromUtf8(Mumble::UpdateHealth::installationKey(m_finalDirectory));
		m_mutex = CreateMutexW(nullptr, FALSE, name.c_str());
		if (!m_mutex) {
			fail("Unable to create the cross-session per-installation updater mutex.");
		}
		const DWORD wait = WaitForSingleObject(m_mutex, 10U * 60U * 1000U);
		if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
			fail("Timed out acquiring the cross-session per-installation updater mutex.");
		}
		m_owned = true;
	}

	~InstallationMutex() {
		if (m_owned) {
			ReleaseMutex(m_mutex);
		}
		if (m_mutex) {
			CloseHandle(m_mutex);
		}
		if (m_directory != INVALID_HANDLE_VALUE) {
			CloseHandle(m_directory);
		}
	}

	InstallationMutex(const InstallationMutex &)            = delete;
	InstallationMutex &operator=(const InstallationMutex &) = delete;

	std::filesystem::path resolvedAppPath(const std::filesystem::path &requestedAppPath) const {
		return m_finalDirectory / requestedAppPath.filename();
	}

private:
	[[noreturn]] void fail(const char *message) {
		if (m_mutex) {
			CloseHandle(m_mutex);
			m_mutex = nullptr;
		}
		if (m_directory != INVALID_HANDLE_VALUE) {
			CloseHandle(m_directory);
			m_directory = INVALID_HANDLE_VALUE;
		}
		throw std::runtime_error(message);
	}

	HANDLE m_mutex = nullptr;
	HANDLE m_directory = INVALID_HANDLE_VALUE;
	bool m_owned    = false;
	std::filesystem::path m_finalDirectory;
};

std::string newTransactionId() {
	HCRYPTPROV provider = 0;
	if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		throw std::runtime_error("Unable to initialize transaction randomness.");
	}
	BYTE bytes[16]{};
	const bool generated = CryptGenRandom(provider, static_cast< DWORD >(sizeof(bytes)), bytes) != FALSE;
	CryptReleaseContext(provider, 0);
	if (!generated) {
		throw std::runtime_error("Unable to generate a transaction identifier.");
	}
	return hexFromBytes(bytes, sizeof(bytes));
}

bool flushFileDurably(const std::filesystem::path &path) {
	HANDLE file =
		CreateFileW(path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	const bool flushed = FlushFileBuffers(file) != FALSE;
	CloseHandle(file);
	return flushed;
}

std::filesystem::path recoveryUpdaterPath(const Options &options) {
	const std::filesystem::path appDir = parentPath(options.appPath);
	const std::string executableHash   = fileSha256(currentExecutablePath());
	return packageWorkRoot(options) / L"recovery" / wideFromUtf8(Mumble::UpdateHealth::installationKey(appDir))
		   / wideFromUtf8(executableHash.substr(0, 16)) / L"mumble-updater-recovery.exe";
}

std::wstring recoveryRunOnceValueName(const Options &options) {
	return L"MumbleUpdateRecovery-" + wideFromUtf8(Mumble::UpdateHealth::installationKey(parentPath(options.appPath)));
}

std::wstring recoveryUpdaterArguments(const Options &options) {
	std::wstring arguments             = L"--recover --app " + quoteArgument(options.appPath);
	const std::filesystem::path appDir = parentPath(options.appPath);
	if (!appDir.empty()) {
		arguments += L" --working-dir " + quoteArgument(appDir.wstring());
	}
	if (!options.updaterLogPath.empty()) {
		arguments += L" --updater-log " + quoteArgument(options.updaterLogPath);
	}
	arguments += L" --no-ui";
	if (options.noRelaunch) {
		arguments += L" --no-relaunch";
	}
	return arguments;
}

bool setRecoveryRunOnce(const Options &options, const std::filesystem::path &recoveryExecutable) {
	HKEY key                         = nullptr;
	// Use a persistent Run value. RunOnce is deleted before execution and leaves
	// a power-loss window in which recovery is no longer armed.
	constexpr wchar_t registryPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	if (RegCreateKeyExW(HKEY_CURRENT_USER, registryPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
		!= ERROR_SUCCESS) {
		return false;
	}
	const std::wstring command = quoteArgument(recoveryExecutable.wstring()) + L" " + recoveryUpdaterArguments(options);
	const std::wstring name    = recoveryRunOnceValueName(options);
	const LSTATUS status =
		RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast< const BYTE * >(command.c_str()),
					   static_cast< DWORD >((command.size() + 1) * sizeof(wchar_t)));
	const LSTATUS flushStatus = status == ERROR_SUCCESS ? RegFlushKey(key) : status;
	RegCloseKey(key);
	return status == ERROR_SUCCESS && flushStatus == ERROR_SUCCESS;
}

bool clearRecoveryRunOnce(const Options &options) {
	HKEY key                         = nullptr;
	constexpr wchar_t registryPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	if (RegOpenKeyExW(HKEY_CURRENT_USER, registryPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
		return false;
	}
	const LSTATUS deleteStatus = RegDeleteValueW(key, recoveryRunOnceValueName(options).c_str());
	const LSTATUS flushStatus  = (deleteStatus == ERROR_SUCCESS || deleteStatus == ERROR_FILE_NOT_FOUND)
								 ? RegFlushKey(key)
								 : deleteStatus;
	RegCloseKey(key);
	return (deleteStatus == ERROR_SUCCESS || deleteStatus == ERROR_FILE_NOT_FOUND) && flushStatus == ERROR_SUCCESS;
}

bool ensureDurableRecoveryFile(const std::filesystem::path &source, const std::filesystem::path &target) {
	if (!fileExists(source.wstring())) {
		return false;
	}
	const std::string sourceHash = fileSha256(source);
	std::error_code error;
	std::filesystem::create_directories(target.parent_path(), error);
	if (error) {
		return false;
	}

	const bool samePath = lowerWide(std::filesystem::absolute(source).wstring())
						  == lowerWide(std::filesystem::absolute(target).wstring());
	const bool reusableTarget = fileExists(target.wstring()) && fileSha256(target) == sourceHash;
	if (!samePath && !reusableTarget) {
		std::filesystem::path temporary = target;
		temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
		std::filesystem::remove(temporary, error);
		error.clear();
		std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing, error);
		if (error || !flushFileDurably(temporary) || fileSha256(temporary) != sourceHash) {
			std::filesystem::remove(temporary, error);
			return false;
		}
		if (!MoveFileExW(temporary.wstring().c_str(), target.wstring().c_str(),
						 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			std::filesystem::remove(temporary, error);
			return fileExists(target.wstring()) && fileSha256(target) == sourceHash;
		}
	}
	return true;
}

bool armRecoveryBootstrap(const Options &options) {
	const std::filesystem::path source = currentExecutablePath();
	const std::filesystem::path target = recoveryUpdaterPath(options);
	if (!ensureDurableRecoveryFile(source, target)) {
		return false;
	}
	return setRecoveryRunOnce(options, target);
}

bool launchRecoveryWatchdog(const Options &options) {
	const std::filesystem::path executable = recoveryUpdaterPath(options);
	if (!fileExists(executable.wstring())) {
		return false;
	}
	std::wstring command = quoteArgument(executable.wstring()) + L" " + recoveryUpdaterArguments(options)
						   + L" --parent-pid " + std::to_wstring(GetCurrentProcessId());
	std::vector< wchar_t > mutableCommand(command.begin(), command.end());
	mutableCommand.push_back(L'\0');

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	const std::wstring workingDirectory = packageWorkRoot(options).wstring();
	auto create                         = [&](const DWORD flags) {
        process = {};
        return CreateProcessW(executable.wstring().c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, flags,
													  nullptr, workingDirectory.c_str(), &startup, &process)
               != FALSE;
	};
	if (!create(CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB)) {
		mutableCommand.assign(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		if (!create(CREATE_NO_WINDOW)) {
			return false;
		}
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

std::uint16_t readLe16(const char *data) {
	const auto *bytes = reinterpret_cast< const unsigned char * >(data);
	return static_cast< std::uint16_t >(bytes[0] | (bytes[1] << 8));
}

std::uint32_t readLe32(const char *data) {
	const auto *bytes = reinterpret_cast< const unsigned char * >(data);
	return static_cast< std::uint32_t >(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
}

struct ZipEntry {
	std::string name;
	std::uint16_t flags             = 0;
	std::uint16_t method            = 0;
	std::uint32_t crc32             = 0;
	std::uint64_t compressedSize    = 0;
	std::uint64_t uncompressedSize  = 0;
	std::uint64_t localHeaderOffset = 0;
};

std::unordered_map< std::string, ZipEntry > readZipDirectory(const std::filesystem::path &packagePath) {
	std::ifstream stream(packagePath, std::ios::binary);
	if (!stream) {
		throw std::runtime_error("Unable to open update package.");
	}

	stream.seekg(0, std::ios::end);
	const std::uint64_t fileSize = static_cast< std::uint64_t >(stream.tellg());
	const std::uint64_t tailSize = std::min< std::uint64_t >(fileSize, 22 + 65535);
	stream.seekg(static_cast< std::streamoff >(fileSize - tailSize), std::ios::beg);

	std::vector< char > tail(static_cast< std::size_t >(tailSize));
	stream.read(tail.data(), static_cast< std::streamsize >(tail.size()));
	if (static_cast< std::size_t >(stream.gcount()) != tail.size()) {
		throw std::runtime_error("Unable to read update package directory.");
	}
	if (tail.size() < 22) {
		throw std::runtime_error("Update package is too small to be a ZIP archive.");
	}

	std::size_t eocdOffset = std::string::npos;
	for (std::size_t offset = tail.size() >= 22 ? tail.size() - 22 : 0; offset != std::string::npos; --offset) {
		if (readLe32(tail.data() + offset) == ZipEndOfCentralDirectorySignature) {
			const std::uint16_t commentLength = readLe16(tail.data() + offset + 20);
			if (offset + 22 + commentLength == tail.size()) {
				eocdOffset = offset;
				break;
			}
		}
		if (offset == 0) {
			break;
		}
	}
	if (eocdOffset == std::string::npos) {
		throw std::runtime_error("Update package is not a supported ZIP archive.");
	}

	const std::uint16_t entryCountDisk           = readLe16(tail.data() + eocdOffset + 8);
	const std::uint16_t entryCount               = readLe16(tail.data() + eocdOffset + 10);
	const std::uint32_t centralDirectorySize32   = readLe32(tail.data() + eocdOffset + 12);
	const std::uint32_t centralDirectoryOffset32 = readLe32(tail.data() + eocdOffset + 16);
	if (entryCountDisk == Zip64Marker16 || entryCount == Zip64Marker16 || centralDirectorySize32 == Zip64Marker32
		|| centralDirectoryOffset32 == Zip64Marker32) {
		throw std::runtime_error("Zip64 update packages are not supported by this updater yet.");
	}

	std::vector< char > directory(centralDirectorySize32);
	stream.seekg(static_cast< std::streamoff >(centralDirectoryOffset32), std::ios::beg);
	stream.read(directory.data(), static_cast< std::streamsize >(directory.size()));
	if (static_cast< std::size_t >(stream.gcount()) != directory.size()) {
		throw std::runtime_error("Unable to read update package central directory.");
	}

	std::unordered_map< std::string, ZipEntry > entries;
	std::size_t offset = 0;
	for (std::uint16_t index = 0; index < entryCount; ++index) {
		if (offset + 46 > directory.size() || readLe32(directory.data() + offset) != ZipCentralDirectorySignature) {
			throw std::runtime_error("Update package central directory is corrupt.");
		}

		ZipEntry entry;
		entry.flags                             = readLe16(directory.data() + offset + 8);
		entry.method                            = readLe16(directory.data() + offset + 10);
		entry.crc32                             = readLe32(directory.data() + offset + 16);
		const std::uint32_t compressedSize32    = readLe32(directory.data() + offset + 20);
		const std::uint32_t uncompressedSize32  = readLe32(directory.data() + offset + 24);
		const std::uint16_t nameLength          = readLe16(directory.data() + offset + 28);
		const std::uint16_t extraLength         = readLe16(directory.data() + offset + 30);
		const std::uint16_t commentLength       = readLe16(directory.data() + offset + 32);
		const std::uint32_t localHeaderOffset32 = readLe32(directory.data() + offset + 42);
		if (compressedSize32 == Zip64Marker32 || uncompressedSize32 == Zip64Marker32
			|| localHeaderOffset32 == Zip64Marker32) {
			throw std::runtime_error("Zip64 update package entries are not supported by this updater yet.");
		}
		if ((entry.flags & 0x1) != 0) {
			throw std::runtime_error("Encrypted update package entries are not supported.");
		}

		const std::size_t nameOffset = offset + 46;
		const std::size_t nextOffset = nameOffset + nameLength + extraLength + commentLength;
		if (nextOffset > directory.size()) {
			throw std::runtime_error("Update package central directory entry is truncated.");
		}

		entry.name.assign(directory.data() + nameOffset, directory.data() + nameOffset + nameLength);
		std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
		entry.compressedSize    = compressedSize32;
		entry.uncompressedSize  = uncompressedSize32;
		entry.localHeaderOffset = localHeaderOffset32;

		if (!entry.name.empty() && entry.name.back() != '/') {
			entries.emplace(entry.name, entry);
		}
		offset = nextOffset;
	}
	return entries;
}

void readZipEntryPayload(const std::filesystem::path &packagePath, const ZipEntry &entry,
						 const std::function< void(const char *, std::size_t) > &writeChunk) {
	std::ifstream stream(packagePath, std::ios::binary);
	if (!stream) {
		throw std::runtime_error("Unable to open update package for extraction.");
	}

	char localHeader[30]{};
	stream.seekg(static_cast< std::streamoff >(entry.localHeaderOffset), std::ios::beg);
	stream.read(localHeader, sizeof(localHeader));
	if (stream.gcount() != sizeof(localHeader) || readLe32(localHeader) != ZipLocalFileHeaderSignature) {
		throw std::runtime_error("Update package local file header is corrupt.");
	}

	const std::uint16_t nameLength  = readLe16(localHeader + 26);
	const std::uint16_t extraLength = readLe16(localHeader + 28);
	stream.seekg(static_cast< std::streamoff >(entry.localHeaderOffset + 30 + nameLength + extraLength), std::ios::beg);

	std::vector< char > input(ZipReadBufferSize);
	std::vector< char > output(ZipReadBufferSize);
	std::uint64_t compressedRemaining = entry.compressedSize;
	std::uint64_t uncompressedWritten = 0;

	if (entry.method == ZipMethodStore) {
		while (compressedRemaining > 0) {
			const std::size_t chunkSize =
				static_cast< std::size_t >(std::min< std::uint64_t >(compressedRemaining, input.size()));
			stream.read(input.data(), static_cast< std::streamsize >(chunkSize));
			if (static_cast< std::size_t >(stream.gcount()) != chunkSize) {
				throw std::runtime_error("Stored update package entry is truncated.");
			}
			writeChunk(input.data(), chunkSize);
			compressedRemaining -= chunkSize;
			uncompressedWritten += chunkSize;
		}
	} else if (entry.method == ZipMethodDeflate) {
		z_stream zstream{};
		if (inflateInit2(&zstream, -MAX_WBITS) != Z_OK) {
			throw std::runtime_error("Unable to initialize ZIP deflate stream.");
		}

		bool finished = false;
		try {
			while (!finished) {
				if (zstream.avail_in == 0 && compressedRemaining > 0) {
					const std::size_t chunkSize =
						static_cast< std::size_t >(std::min< std::uint64_t >(compressedRemaining, input.size()));
					stream.read(input.data(), static_cast< std::streamsize >(chunkSize));
					if (static_cast< std::size_t >(stream.gcount()) != chunkSize) {
						throw std::runtime_error("Deflated update package entry is truncated.");
					}
					compressedRemaining -= chunkSize;
					zstream.next_in  = reinterpret_cast< Bytef * >(input.data());
					zstream.avail_in = static_cast< uInt >(chunkSize);
				}

				zstream.next_out  = reinterpret_cast< Bytef * >(output.data());
				zstream.avail_out = static_cast< uInt >(output.size());
				const int result  = inflate(&zstream, Z_NO_FLUSH);
				if (result != Z_OK && result != Z_STREAM_END) {
					throw std::runtime_error("Unable to inflate update package entry.");
				}

				const std::size_t produced = output.size() - zstream.avail_out;
				if (produced > 0) {
					writeChunk(output.data(), produced);
					uncompressedWritten += produced;
				}
				finished = result == Z_STREAM_END;

				if (compressedRemaining == 0 && zstream.avail_in == 0 && !finished) {
					throw std::runtime_error("Deflated update package entry ended before the stream completed.");
				}
			}
		} catch (...) {
			inflateEnd(&zstream);
			throw;
		}
		inflateEnd(&zstream);
	} else {
		throw std::runtime_error("Unsupported ZIP compression method in update package.");
	}

	if (uncompressedWritten != entry.uncompressedSize) {
		throw std::runtime_error("Update package entry size mismatch after extraction.");
	}
}

std::string readZipEntryText(const std::filesystem::path &packagePath, const ZipEntry &entry) {
	std::string contents;
	contents.reserve(static_cast< std::size_t >(std::min< std::uint64_t >(entry.uncompressedSize, 1024 * 1024)));
	readZipEntryPayload(packagePath, entry,
						[&contents](const char *data, const std::size_t size) { contents.append(data, data + size); });
	return contents;
}

void extractZipEntryToFile(const Options &options, const std::filesystem::path &packagePath, const ZipEntry &entry,
						   const std::filesystem::path &target, const std::string &expectedSha256,
						   const std::uint64_t expectedSize) {
	std::error_code error;
	std::filesystem::create_directories(target.parent_path(), error);
	if (error) {
		throw std::runtime_error("Unable to create staged package directory.");
	}

	std::ofstream output(target, std::ios::binary | std::ios::trunc);
	if (!output) {
		throw std::runtime_error("Unable to create staged package file: " + utf8FromWide(target.wstring()));
	}

	Sha256Hasher hasher;
	std::uint64_t written = 0;
	readZipEntryPayload(packagePath, entry, [&](const char *data, const std::size_t size) {
		output.write(data, static_cast< std::streamsize >(size));
		if (!output) {
			throw std::runtime_error("Unable to write staged package file.");
		}
		hasher.add(data, size);
		written += size;
	});
	output.close();
	if (!output) {
		throw std::runtime_error("Unable to finish staged package file.");
	}

	if (written != expectedSize) {
		std::filesystem::remove(target, error);
		throw std::runtime_error("Staged package file size mismatch.");
	}
	const std::string actualSha256 = hasher.finishHex();
	if (actualSha256 != expectedSha256) {
		std::filesystem::remove(target, error);
		throw std::runtime_error("Staged package file SHA256 mismatch.");
	}
	appendLog(options, L"Staged " + wideFromUtf8(entry.name) + L'.');
}

struct PackageFile {
	std::string path;
	std::uint64_t size = 0;
	std::string sha256;
	bool changed = true;
};

struct PackagePlan {
	std::string packageIdentity;
	std::filesystem::path packagePath;
	std::filesystem::path appPath;
	std::filesystem::path appDir;
	std::filesystem::path updateRoot;
	std::filesystem::path stageRoot;
	std::filesystem::path sidecarPath;
	std::vector< PackageFile > files;
	std::vector< PackageFile > changedFiles;
	std::vector< PackageFile > staleFiles;
	std::string previousPackageIdentity;
	std::string expectedExecutableSha256;
	bool healthCheckRequired                       = false;
	std::uint64_t minimumStableRuntimeMilliseconds = Mumble::UpdateHealth::MinimumStableRuntimeMilliseconds;
	std::uint64_t healthTimeoutMilliseconds        = Mumble::UpdateHealth::DefaultHealthTimeoutMilliseconds;
};

std::string appDirManifestKey(const std::filesystem::path &appDir) {
	return Mumble::UpdateHealth::installationKey(appDir);
}

std::filesystem::path installedManifestPath(const PackagePlan &plan) {
	return plan.updateRoot / L"installed-manifests" / (wideFromUtf8(appDirManifestKey(plan.appDir)) + L".json");
}

PackagePlan makePackagePlan(const Options &options) {
	PackagePlan plan;
	plan.packageIdentity = lowerSha256(options.packageSha256);
	if (plan.packageIdentity.size() != 64 || fileSha256(options.packagePath) != plan.packageIdentity) {
		throw std::runtime_error("Update package is not bound to its required SHA256.");
	}
	plan.packagePath = std::filesystem::path(options.packagePath);
	plan.appPath     = std::filesystem::path(options.appPath);
	plan.appDir      = plan.appPath.parent_path();
	plan.updateRoot  = packageWorkRoot(options);
	plan.stageRoot   = plan.updateRoot / L"prepared-packages" / wideFromUtf8(plan.packageIdentity);
	plan.sidecarPath = std::filesystem::path(pathWithSuffix(plan.packagePath, L".prepared.json"));
	return plan;
}

struct InstalledPackageState {
	std::string packageIdentity;
	std::unordered_map< std::string, PackageFile > files;
};

InstalledPackageState loadInstalledPackageState(const PackagePlan &plan) {
	InstalledPackageState state;
	const std::filesystem::path manifestPath = installedManifestPath(plan);
	if (!fileExists(manifestPath.wstring())) {
		return state;
	}

	try {
		std::ifstream stream(manifestPath, std::ios::binary);
		const json manifest   = json::parse(stream);
		state.packageIdentity = manifest.value("packageIdentity", "");
		for (const json &file : manifest.value("files", json::array())) {
			PackageFile installed;
			installed.path   = file.value("path", "");
			installed.size   = file.value("size", static_cast< std::uint64_t >(0));
			installed.sha256 = lowerAscii(file.value("sha256", ""));
			if (isSafePackageRelativePath(installed.path) && !installed.sha256.empty()) {
				state.files[installed.path] = std::move(installed);
			}
		}
	} catch (...) {
		state = {};
	}
	return state;
}

std::uint64_t fileSizeOrMissing(const std::filesystem::path &path, bool &exists) {
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	exists                    = !error;
	return exists ? static_cast< std::uint64_t >(size) : 0;
}

std::vector< PackageFile > parsePackageManifest(const std::string &manifestText, PackagePlan &plan) {
	const json manifest = json::parse(manifestText);
	if (manifest.value("format", "") != "mumble-update-v1") {
		throw std::runtime_error("Unsupported update package format.");
	}
	if (manifest.value("applyMode", "") != "replace-staged-payload") {
		throw std::runtime_error("Unsupported update package apply mode.");
	}
	const auto updaterVersionIt = manifest.find("minUpdaterVersion");
	if (updaterVersionIt == manifest.end() || !updaterVersionIt->is_number_integer()
		|| updaterVersionIt->get< std::int64_t >()
			   != static_cast< std::int64_t >(Mumble::UpdateHealth::UpdaterProtocolVersion)) {
		throw std::runtime_error("Update package does not require this exact updater protocol.");
	}

	const auto healthCheckIt = manifest.find("healthCheck");
	if (healthCheckIt == manifest.end() || !healthCheckIt->is_object()) {
		throw std::runtime_error("Update package is missing the mandatory health-check contract.");
	}
	const json &healthCheck = *healthCheckIt;
	if (healthCheck.size() != 3 || !healthCheck.contains("required")
		|| !healthCheck.contains("minimumStableRuntimeMilliseconds")
		|| !healthCheck.contains("timeoutMilliseconds") || !healthCheck.at("required").is_boolean()
		|| !healthCheck.at("required").get< bool >()
		|| !healthCheck.at("minimumStableRuntimeMilliseconds").is_number_integer()
		|| !healthCheck.at("timeoutMilliseconds").is_number_integer()) {
		throw std::runtime_error("Update package health-check contract is invalid.");
	}
	const std::int64_t minimumStableRuntimeMilliseconds =
		healthCheck.at("minimumStableRuntimeMilliseconds").get< std::int64_t >();
	const std::int64_t healthTimeoutMilliseconds =
		healthCheck.at("timeoutMilliseconds").get< std::int64_t >();
	if (minimumStableRuntimeMilliseconds
			!= static_cast< std::int64_t >(Mumble::UpdateHealth::MinimumStableRuntimeMilliseconds)
		|| healthTimeoutMilliseconds
			   != static_cast< std::int64_t >(Mumble::UpdateHealth::DefaultHealthTimeoutMilliseconds)) {
		throw std::runtime_error("Update package health-check timing does not match the protocol-v4 contract.");
	}
	plan.healthCheckRequired              = true;
	plan.minimumStableRuntimeMilliseconds = static_cast< std::uint64_t >(minimumStableRuntimeMilliseconds);
	plan.healthTimeoutMilliseconds        = static_cast< std::uint64_t >(healthTimeoutMilliseconds);

	std::vector< PackageFile > files;
	std::unordered_set< std::string > uniquePaths;
	bool hasMumble  = false;
	bool hasUpdater = false;
	for (const json &entry : manifest.value("files", json::array())) {
		PackageFile file;
		file.path = entry.value("path", "");
		std::replace(file.path.begin(), file.path.end(), '\\', '/');
		file.size   = entry.value("size", static_cast< std::uint64_t >(0));
		file.sha256 = lowerAscii(entry.value("sha256", ""));
		const bool validHash = file.sha256.size() == 64
						   && std::all_of(file.sha256.begin(), file.sha256.end(), [](const unsigned char ch) {
							  return std::isxdigit(ch) != 0;
						   });
		if (!isSafePackageRelativePath(file.path) || !validHash || !uniquePaths.insert(file.path).second) {
			throw std::runtime_error("Update package manifest contains an invalid file entry.");
		}
		if (file.path == "mumble.exe") {
			hasMumble                    = true;
			plan.expectedExecutableSha256 = file.sha256;
		}
		hasUpdater = hasUpdater || file.path == "mumble-updater.exe";
		files.push_back(std::move(file));
	}
	if (files.empty()) {
		throw std::runtime_error("Update package manifest does not list any files.");
	}
	if (!hasMumble || !hasUpdater) {
		throw std::runtime_error("Update package is missing required Mumble executables.");
	}
	return files;
}

void writeJsonFile(const std::filesystem::path &path, const json &document) {
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error) {
		throw std::runtime_error("Unable to create JSON output directory.");
	}

	const std::filesystem::path tempPath(pathWithSuffix(path, L".tmp"));
	{
		std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
		if (!stream) {
			throw std::runtime_error("Unable to write JSON output file.");
		}
		stream << document.dump(2);
		stream << '\n';
	}
	if (!flushFileDurably(tempPath)) {
		std::filesystem::remove(tempPath, error);
		throw std::runtime_error("Unable to durably finish JSON output file.");
	}
	if (!MoveFileExW(tempPath.wstring().c_str(), path.wstring().c_str(),
					 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		std::filesystem::remove(tempPath, error);
		throw std::runtime_error("Unable to durably commit JSON output file.");
	}
}

void writePreparedSidecar(const PackagePlan &plan) {
	json files        = json::array();
	json changedFiles = json::array();
	json staleFiles   = json::array();
	for (const PackageFile &file : plan.files) {
		json entry{
			{ "path", file.path },
			{ "size", file.size },
			{ "sha256", file.sha256 },
			{ "changed", file.changed },
		};
		files.push_back(entry);
		if (file.changed) {
			changedFiles.push_back(entry);
		}
	}
	for (const PackageFile &file : plan.staleFiles) {
		staleFiles.push_back(json{
			{ "path", file.path },
			{ "size", file.size },
			{ "sha256", file.sha256 },
		});
	}

	writeJsonFile(plan.sidecarPath, json{
										{ "schema", 1 },
										{ "packageIdentity", plan.packageIdentity },
										{ "packagePath", utf8FromWide(plan.packagePath.wstring()) },
										{ "appPath", utf8FromWide(plan.appPath.wstring()) },
										{ "stageRoot", utf8FromWide(plan.stageRoot.wstring()) },
										{ "previousPackageIdentity", plan.previousPackageIdentity },
										{ "healthCheckRequired", plan.healthCheckRequired },
										{ "minimumStableRuntimeMilliseconds", plan.minimumStableRuntimeMilliseconds },
										{ "healthTimeoutMilliseconds", plan.healthTimeoutMilliseconds },
										{ "files", files },
										{ "changedFiles", changedFiles },
										{ "staleFiles", staleFiles },
									});
}

PackagePlan loadPreparedSidecar(const Options &options) {
	PackagePlan plan = makePackagePlan(options);
	std::ifstream stream(plan.sidecarPath, std::ios::binary);
	if (!stream) {
		throw std::runtime_error("Prepared update sidecar is missing.");
	}
	const json sidecar         = json::parse(stream);
	if (sidecar.value("schema", 0) != 1) {
		throw std::runtime_error("Prepared update sidecar has an unsupported schema.");
	}
	const std::string identity = lowerAscii(sidecar.value("packageIdentity", ""));
	if (identity != plan.packageIdentity) {
		throw std::runtime_error("Prepared update identity does not match the requested package.");
	}
	const std::wstring sidecarPackagePath = wideFromUtf8(sidecar.value("packagePath", ""));
	if (!sidecarPackagePath.empty()
		&& lowerWide(std::filesystem::absolute(sidecarPackagePath).wstring())
			   != lowerWide(std::filesystem::absolute(plan.packagePath).wstring())) {
		throw std::runtime_error("Prepared update package path does not match the requested package.");
	}
	const std::wstring sidecarAppPath = wideFromUtf8(sidecar.value("appPath", ""));
	if (!sidecarAppPath.empty()
		&& lowerWide(std::filesystem::absolute(sidecarAppPath).wstring())
			   != lowerWide(std::filesystem::absolute(plan.appPath).wstring())) {
		throw std::runtime_error("Prepared update app path does not match the requested app.");
	}

	const std::wstring sidecarStageRoot = wideFromUtf8(sidecar.value("stageRoot", ""));
	if (sidecarStageRoot.empty()
		|| lowerWide(std::filesystem::absolute(sidecarStageRoot).lexically_normal().wstring())
			   != lowerWide(std::filesystem::absolute(plan.stageRoot).lexically_normal().wstring())) {
		throw std::runtime_error("Prepared update stage root does not match the verified package identity.");
	}

	// The public sidecar is only a cache-complete signal. Paths, hashes, health
	// policy, changed files, and stale files are reconstructed from the locked,
	// SHA-verified ZIP and current installation; none are trusted from sidecar.
	const auto zipEntries = readZipDirectory(plan.packagePath);
	const auto manifestIt = zipEntries.find("manifest.json");
	if (manifestIt == zipEntries.end()) {
		throw std::runtime_error("Verified update package is missing manifest.json.");
	}
	plan.files = parsePackageManifest(readZipEntryText(plan.packagePath, manifestIt->second), plan);

	const InstalledPackageState installedState = loadInstalledPackageState(plan);
	plan.previousPackageIdentity               = installedState.packageIdentity;
	std::unordered_set< std::string > nextPaths;
	for (PackageFile &file : plan.files) {
		nextPaths.insert(file.path);
		const auto target              = resolvePackagePathUnderRoot(plan.appDir, file.path);
		bool targetExists              = false;
		const std::uint64_t targetSize = fileSizeOrMissing(target, targetExists);
		file.changed                   = !targetExists || targetSize != file.size || fileSha256(target) != file.sha256;
		if (file.changed) {
			const auto source              = resolvePackagePathUnderRoot(plan.stageRoot / L"payload", file.path);
			bool sourceExists              = false;
			const std::uint64_t sourceSize = fileSizeOrMissing(source, sourceExists);
			if (!sourceExists || sourceSize != file.size || fileSha256(source) != file.sha256) {
				throw std::runtime_error("Prepared package cache is incomplete for " + file.path + ".");
			}
			plan.changedFiles.push_back(file);
		}
	}
	for (const auto &[path, installed] : installedState.files) {
		if (nextPaths.find(path) != nextPaths.end()) {
			continue;
		}
		const auto target = resolvePackagePathUnderRoot(plan.appDir, path);
		if (!fileExists(target.wstring())) {
			continue;
		}
		bool targetExists              = false;
		const std::uint64_t targetSize = fileSizeOrMissing(target, targetExists);
		if (!targetExists || targetSize != installed.size || fileSha256(target) != installed.sha256) {
			throw std::runtime_error("Refusing to delete a locally modified stale managed file: " + path);
		}
		plan.staleFiles.push_back(installed);
	}
	return plan;
}

void writeInstalledManifest(const PackagePlan &plan) {
	json files = json::array();
	for (const PackageFile &file : plan.files) {
		files.push_back(json{
			{ "path", file.path },
			{ "size", file.size },
			{ "sha256", file.sha256 },
		});
	}

	writeJsonFile(installedManifestPath(plan), json{
												   { "schema", 1 },
												   { "appPath", utf8FromWide(plan.appPath.wstring()) },
												   { "appDir", utf8FromWide(plan.appDir.wstring()) },
												   { "packageIdentity", plan.packageIdentity },
												   { "files", files },
											   });
}

DWORD prepareNativePackageUpdate(const Options &options) {
	try {
		PackagePlan plan = makePackagePlan(options);
		appendLog(options, L"Preparing update package.");
		postUiProgress(-1, true);

		const auto zipEntries = readZipDirectory(plan.packagePath);
		const auto manifestIt = zipEntries.find("manifest.json");
		if (manifestIt == zipEntries.end()) {
			throw std::runtime_error("Update package is missing manifest.json.");
		}
		plan.files = parsePackageManifest(readZipEntryText(plan.packagePath, manifestIt->second), plan);

		std::error_code error;
		if (std::filesystem::exists(plan.stageRoot, error)) {
			std::filesystem::remove_all(plan.stageRoot, error);
			if (error) {
				throw std::runtime_error("Unable to remove stale prepared package directory.");
			}
		}
		std::filesystem::create_directories(plan.stageRoot / L"payload", error);
		if (error) {
			throw std::runtime_error("Unable to create prepared package directory.");
		}

		const InstalledPackageState installedState = loadInstalledPackageState(plan);
		plan.previousPackageIdentity               = installedState.packageIdentity;
		int progress                               = 0;
		int totalProgress                          = 1;
		const auto progressLog                     = [&](const std::wstring &message) {
            ++progress;
            appendLog(options, L"Progress " + std::to_wstring(progress) + L"/" + std::to_wstring(totalProgress) + L": "
													   + message);
		};

		for (PackageFile &file : plan.files) {
			appendLog(options, L"Planning file " + wideFromUtf8(file.path) + L'.');
			const auto target              = resolvePackagePathUnderRoot(plan.appDir, file.path);
			bool targetExists              = false;
			const std::uint64_t targetSize = fileSizeOrMissing(target, targetExists);
			file.changed                   = true;
			const auto installedIt         = installedState.files.find(file.path);
			if (targetExists && targetSize == file.size && installedIt != installedState.files.end()
				&& installedIt->second.sha256 == file.sha256) {
				file.changed = false;
			} else if (targetExists && targetSize == file.size) {
				file.changed = fileSha256(target) != file.sha256;
			}
			if (file.changed) {
				plan.changedFiles.push_back(file);
			}
		}

		std::unordered_set< std::string > nextPaths;
		for (const PackageFile &file : plan.files) {
			nextPaths.insert(file.path);
		}
		for (const auto &[path, installed] : installedState.files) {
			if (nextPaths.find(path) != nextPaths.end()) {
				continue;
			}
			const auto target = resolvePackagePathUnderRoot(plan.appDir, path);
			if (fileExists(target.wstring())) {
				plan.staleFiles.push_back(installed);
			}
		}

		progress      = 0;
		totalProgress = static_cast< int >(plan.changedFiles.size() + 1);
		for (const PackageFile &file : plan.changedFiles) {
			progressLog(L"Staging file " + wideFromUtf8(file.path));
			const std::string zipName = "payload/" + file.path;
			const auto entryIt        = zipEntries.find(zipName);
			if (entryIt == zipEntries.end()) {
				throw std::runtime_error("Update package payload is missing " + file.path + ".");
			}
			const auto stagedPath = resolvePackagePathUnderRoot(plan.stageRoot / L"payload", file.path);
			extractZipEntryToFile(options, plan.packagePath, entryIt->second, stagedPath, file.sha256, file.size);
		}

		writePreparedSidecar(plan);
		progressLog(L"Prepared update package");
		appendLog(options, L"Prepared update package with " + std::to_wstring(plan.changedFiles.size())
							   + L" changed files and " + std::to_wstring(plan.staleFiles.size())
							   + L" stale managed files.");
		postUiProgress(100, false);
		return 0;
	} catch (const std::exception &exception) {
		appendLog(options, L"Package prepare failed. " + wideFromUtf8(exception.what()));
		return 1;
	}
}

struct BackupEntry {
	std::string relativePath;
	std::filesystem::path target;
	std::filesystem::path backup;
	bool existed       = false;
	std::uint64_t size = 0;
	std::string sha256;
};

BackupEntry backupManagedTarget(const PackagePlan &plan, const std::filesystem::path &backupRoot,
								const std::string &relativePath) {
	BackupEntry backup;
	backup.relativePath = relativePath;
	backup.target       = resolvePackagePathUnderRoot(plan.appDir, relativePath);
	backup.backup       = resolvePackagePathUnderRoot(backupRoot, relativePath);
	backup.existed      = fileExists(backup.target.wstring());
	if (!backup.existed) {
		return backup;
	}

	std::error_code error;
	std::filesystem::create_directories(backup.backup.parent_path(), error);
	if (error) {
		throw std::runtime_error("Unable to create backup directory for " + relativePath);
	}
	std::filesystem::copy_file(backup.target, backup.backup, std::filesystem::copy_options::overwrite_existing, error);
	if (error) {
		throw std::runtime_error("Unable to back up " + relativePath);
	}
	if (!flushFileDurably(backup.backup)) {
		throw std::runtime_error("Unable to durably back up " + relativePath);
	}
	bool backupExists = false;
	backup.size       = fileSizeOrMissing(backup.backup, backupExists);
	backup.sha256     = backupExists ? fileSha256(backup.backup) : std::string();
	if (!backupExists || backup.sha256.empty()) {
		throw std::runtime_error("Unable to verify backup for " + relativePath);
	}
	return backup;
}

void restoreBackupEntries(const std::vector< BackupEntry > &backups) {
	std::error_code error;
	for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
		if (!it->existed) {
			if (fileExists(it->target.wstring())) {
				std::filesystem::remove(it->target, error);
				if (error || fileExists(it->target.wstring())) {
					throw std::runtime_error("Unable to remove newly installed file " + it->relativePath);
				}
			}
			continue;
		}
		bool backupExists              = false;
		const std::uint64_t backupSize = fileSizeOrMissing(it->backup, backupExists);
		if (!backupExists || backupSize != it->size || fileSha256(it->backup) != it->sha256) {
			throw std::runtime_error("Known-good backup failed verification for " + it->relativePath);
		}
		std::filesystem::create_directories(it->target.parent_path(), error);
		if (error) {
			throw std::runtime_error("Unable to recreate target directory for " + it->relativePath);
		}
		std::filesystem::copy_file(it->backup, it->target, std::filesystem::copy_options::overwrite_existing, error);
		if (error || !flushFileDurably(it->target)) {
			throw std::runtime_error("Unable to restore " + it->relativePath);
		}
		bool restoredExists              = false;
		const std::uint64_t restoredSize = fileSizeOrMissing(it->target, restoredExists);
		if (!restoredExists || restoredSize != it->size || fileSha256(it->target) != it->sha256) {
			throw std::runtime_error("Restored file failed verification for " + it->relativePath);
		}
	}
}

bool pendingOwnsExpectedBackupRoot(const std::filesystem::path &updateRoot,
								  const Mumble::UpdateHealth::PendingUpdate &pending) {
	std::filesystem::path expected;
	try {
		const std::string relative = "known-good/"
								 + Mumble::UpdateHealth::installationKey(pending.appPath.parent_path())
								 + "/transaction-" + pending.transactionId;
		expected = resolvePackagePathUnderRoot(updateRoot, relative);
	} catch (...) {
		return false;
	}
	if (lowerWide(std::filesystem::absolute(pending.backupRoot).lexically_normal().wstring())
		!= lowerWide(std::filesystem::absolute(expected).lexically_normal().wstring())) {
		return false;
	}
	const DWORD attributes = GetFileAttributesW(pending.backupRoot.wstring().c_str());
	return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool persistTerminalTransaction(const Options &options, const std::filesystem::path &updateRoot,
								 Mumble::UpdateHealth::PendingUpdate &pending,
								 const Mumble::UpdateHealth::TransactionState terminalState,
								 std::string &healthError) {
	pending.state = terminalState;
	if (!Mumble::UpdateHealth::writePendingState(updateRoot, pending, &healthError)) {
		return false;
	}
	if (!pendingOwnsExpectedBackupRoot(updateRoot, pending)) {
		healthError = "Terminal transaction backup root is not its expected non-reparse snapshot.";
		appendLog(options, L"Refusing terminal cleanup outside the transaction-owned backup root.");
		return false;
	}

	// Terminal state is written before cleanup. If power is lost during any
	// following deletion, recovery can only resume cleanup and can never roll a
	// fully committed transaction back (or reapply a completed rollback).
	std::error_code error;
	std::filesystem::remove(Mumble::UpdateHealth::healthMarkerPath(updateRoot, pending), error);
	if (error) {
		appendLog(options, L"Terminal transaction retained because its health marker could not be removed.");
		return true;
	}
	std::filesystem::remove_all(pending.backupRoot, error);
	if (error) {
		appendLog(options, L"Terminal transaction retained because its backup could not be removed.");
		return true;
	}
	if (!Mumble::UpdateHealth::removePendingState(updateRoot, pending.appPath, &healthError)) {
		appendLog(options, L"Terminal transaction retained for retry. " + wideFromUtf8(healthError));
		return true;
	}
	if (!clearRecoveryRunOnce(options)) {
		// The persistent trigger is safe to leave behind: on the next logon it
		// observes no journal and retries its own removal.
		appendLog(options, L"Persistent recovery trigger will remove itself on its next run.");
	}
	return true;
}

DWORD commitNativePackageUpdate(const Options &options) {
	try {
		PackagePlan plan;
		try {
			plan = loadPreparedSidecar(options);
		} catch (const std::exception &exception) {
			appendLog(options,
					  L"Prepared package is unavailable; preparing during install. " + wideFromUtf8(exception.what()));
			const DWORD prepareExitCode = prepareNativePackageUpdate(options);
			if (prepareExitCode != 0) {
				return prepareExitCode;
			}
			plan = loadPreparedSidecar(options);
		}

		appendLog(options, L"Committing prepared update package.");
		const std::string transactionId = newTransactionId();
		const std::string backupRelative = "known-good/" + Mumble::UpdateHealth::installationKey(plan.appDir)
										   + "/transaction-" + transactionId;
		const std::filesystem::path backupRoot = resolvePackagePathUnderRoot(plan.updateRoot, backupRelative);
		std::vector< BackupEntry > backups;
		std::error_code error;
		std::filesystem::create_directories(backupRoot, error);
		if (error) {
			throw std::runtime_error("Unable to create package backup directory.");
		}

		int progress = 0;
		const int totalProgress =
			static_cast< int >(std::max< std::size_t >(1, plan.changedFiles.size() + plan.staleFiles.size()) + 2);
		const auto progressLog = [&](const std::wstring &message) {
			++progress;
			appendLog(options, L"Progress " + std::to_wstring(progress) + L"/" + std::to_wstring(totalProgress) + L": "
								   + message);
		};

		const std::filesystem::path currentManifest        = installedManifestPath(plan);
		const std::filesystem::path previousManifestBackup = backupRoot / L"installed-manifest.json";
		const bool previousManifestExisted                 = fileExists(currentManifest.wstring());
		std::uint64_t previousManifestSize                 = 0;
		std::string previousManifestSha256;
		if (previousManifestExisted) {
			std::filesystem::copy_file(currentManifest, previousManifestBackup,
									   std::filesystem::copy_options::overwrite_existing, error);
			if (error) {
				throw std::runtime_error("Unable to preserve the installed package manifest.");
			}
			if (!flushFileDurably(previousManifestBackup)) {
				throw std::runtime_error("Unable to durably preserve the installed package manifest.");
			}
			bool copied            = false;
			previousManifestSize   = fileSizeOrMissing(previousManifestBackup, copied);
			previousManifestSha256 = copied ? fileSha256(previousManifestBackup) : std::string();
			if (!copied || previousManifestSha256.empty()) {
				throw std::runtime_error("Unable to verify the preserved installed package manifest.");
			}
		}

		// Validate every staged source and durably capture every previous target
		// before the pending journal permits the first application mutation.
		for (const PackageFile &file : plan.changedFiles) {
			const auto source              = resolvePackagePathUnderRoot(plan.stageRoot / L"payload", file.path);
			bool sourceExists              = false;
			const std::uint64_t sourceSize = fileSizeOrMissing(source, sourceExists);
			if (!sourceExists || sourceSize != file.size || fileSha256(source) != file.sha256) {
				throw std::runtime_error("Prepared payload file is missing or invalid: " + file.path);
			}
		}
		for (const PackageFile &file : plan.changedFiles) {
			backups.push_back(backupManagedTarget(plan, backupRoot, file.path));
		}
		for (const PackageFile &file : plan.staleFiles) {
			BackupEntry backup = backupManagedTarget(plan, backupRoot, file.path);
			if (!backup.existed || backup.size != file.size || backup.sha256 != file.sha256) {
				throw std::runtime_error("Refusing to delete a stale managed file whose snapshot changed: " + file.path);
			}
			backups.push_back(std::move(backup));
		}

		Mumble::UpdateHealth::PendingUpdate pending;
		pending.transactionId                    = transactionId;
		pending.state                            = Mumble::UpdateHealth::TransactionState::RollbackArmed;
		pending.packageIdentity                  = plan.packageIdentity;
		pending.previousPackageIdentity          = plan.previousPackageIdentity;
		pending.expectedExecutableSha256         = plan.expectedExecutableSha256;
		pending.appPath                          = plan.appPath;
		pending.backupRoot                       = backupRoot;
		pending.previousInstalledManifestExisted = previousManifestExisted;
		pending.previousInstalledManifestSize    = previousManifestSize;
		pending.previousInstalledManifestSha256  = previousManifestSha256;
		pending.minimumStableRuntimeMilliseconds = plan.minimumStableRuntimeMilliseconds;
		pending.healthTimeoutMilliseconds        = plan.healthTimeoutMilliseconds;
		for (const BackupEntry &backup : backups) {
			pending.rollbackFiles.push_back(Mumble::UpdateHealth::RollbackFile{
				backup.relativePath,
				backup.existed,
				backup.size,
				backup.sha256,
			});
		}

		if (!armRecoveryBootstrap(options)) {
			throw std::runtime_error("Unable to install the persistent update recovery bootstrap.");
		}
		std::string healthError;
		if (!Mumble::UpdateHealth::writePendingState(plan.updateRoot, pending, &healthError)) {
			clearRecoveryRunOnce(options);
			throw std::runtime_error("Unable to durably arm update rollback before mutation: " + healthError);
		}
		appendLog(options, L"Durably armed update rollback before the first managed-file mutation.");
#ifdef MUMBLE_UPDATER_TEST_HOOKS
		const bool skipRecoveryWatchdog = options.testSkipRecoveryWatchdog;
#else
		constexpr bool skipRecoveryWatchdog = false;
#endif
		if (!skipRecoveryWatchdog && !launchRecoveryWatchdog(options)) {
			Mumble::UpdateHealth::removePendingState(plan.updateRoot, plan.appPath, &healthError);
			clearRecoveryRunOnce(options);
			throw std::runtime_error("Unable to launch the update recovery watchdog before mutation.");
		}
#ifdef MUMBLE_UPDATER_TEST_HOOKS
		if (options.testCrashAfterJournal) {
			ExitProcess(ERROR_PROCESS_ABORTED);
		}
#endif

		try {
			std::size_t mutationCount = 0;
			for (const PackageFile &file : plan.changedFiles) {
				progressLog(L"Installing file " + wideFromUtf8(file.path));
				const auto source = resolvePackagePathUnderRoot(plan.stageRoot / L"payload", file.path);
				const auto target = resolvePackagePathUnderRoot(plan.appDir, file.path);
				std::filesystem::create_directories(target.parent_path(), error);
				if (error) {
					throw std::runtime_error("Unable to create target directory for " + file.path);
				}

				std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
				if (error || !flushFileDurably(target)) {
					throw std::runtime_error("Unable to durably install " + file.path);
				}

				bool targetExists              = false;
				const std::uint64_t targetSize = fileSizeOrMissing(target, targetExists);
				if (!targetExists || targetSize != file.size || fileSha256(target) != file.sha256) {
					throw std::runtime_error("Installed file size or SHA256 mismatch for " + file.path);
				}
				++mutationCount;
#ifdef MUMBLE_UPDATER_TEST_HOOKS
				if (options.testCrashAfterFirstMutation && mutationCount == 1) {
					ExitProcess(ERROR_PROCESS_ABORTED);
				}
#endif
			}

			for (const PackageFile &file : plan.staleFiles) {
				progressLog(L"Removing stale managed file " + wideFromUtf8(file.path));
				const auto target = resolvePackagePathUnderRoot(plan.appDir, file.path);
				bool targetExists              = false;
				const std::uint64_t targetSize = fileSizeOrMissing(target, targetExists);
				if (!targetExists || targetSize != file.size || fileSha256(target) != file.sha256) {
					throw std::runtime_error("Refusing to delete a stale managed file modified during update: " + file.path);
				}
				if (!std::filesystem::remove(target, error) || error || fileExists(target.wstring())) {
					throw std::runtime_error("Unable to remove stale managed file " + file.path);
				}
				++mutationCount;
#ifdef MUMBLE_UPDATER_TEST_HOOKS
				if (options.testCrashAfterFirstMutation && mutationCount == 1) {
					ExitProcess(ERROR_PROCESS_ABORTED);
				}
#endif
			}

			writeInstalledManifest(plan);
			progressLog(L"Recorded installed package manifest");
			if (plan.healthCheckRequired) {
				pending.state = Mumble::UpdateHealth::TransactionState::AwaitingHealth;
				if (!Mumble::UpdateHealth::writePendingState(plan.updateRoot, pending, &healthError)) {
					throw std::runtime_error("Unable to enter durable awaiting-health state: " + healthError);
				}
				appendLog(options, L"Awaiting ten seconds of stable client audio before committing the update.");
			} else {
				if (!persistTerminalTransaction(options, plan.updateRoot, pending,
											Mumble::UpdateHealth::TransactionState::Committed, healthError)) {
					throw std::runtime_error("Unable to durably commit completed update transaction: " + healthError);
				}
			}
		} catch (...) {
			appendLog(options, L"Package apply failed; restoring backup.");
			restoreBackupEntries(backups);
			if (previousManifestExisted) {
				std::filesystem::copy_file(previousManifestBackup, currentManifest,
										   std::filesystem::copy_options::overwrite_existing, error);
				if (error || !flushFileDurably(currentManifest)
					|| fileSha256(currentManifest) != previousManifestSha256) {
					throw std::runtime_error("Unable to durably restore the installed package manifest.");
				}
			} else {
				std::filesystem::remove(currentManifest, error);
				if (error || fileExists(currentManifest.wstring())) {
					throw std::runtime_error("Unable to remove the replacement installed package manifest.");
				}
			}
			if (!persistTerminalTransaction(options, plan.updateRoot, pending,
										Mumble::UpdateHealth::TransactionState::RolledBack, healthError)) {
				throw std::runtime_error("Unable to record the restored update transaction: " + healthError);
			}
			throw;
		}

		std::filesystem::remove_all(plan.stageRoot, error);
		std::filesystem::remove(plan.sidecarPath, error);
		progressLog(L"Update package applied successfully");
		appendLog(options, L"Update package applied successfully.");
		postUiProgress(100, false);
		return 0;
	} catch (const std::exception &exception) {
		appendLog(options, L"Package apply failed. " + wideFromUtf8(exception.what()));
		return 1;
	}
}

bool rollbackPendingPackage(const Options &options);

DWORD runPackageUpdate(const Options &options) {
	// Native package transactions intentionally remain same-user. Elevating this
	// generic file-replacement path would turn user-writable ZIP/journal state
	// into a trusted Program Files writer. Machine installs use the verified MSI
	// fallback and Windows Installer's privileged transaction boundary instead.
	if (processIsElevated()) {
		appendLog(options, L"Refusing native package apply from an elevated token; use the signed MSI fallback.");
		return ERROR_ELEVATION_REQUIRED;
	}
	if (options.prepareOnly) {
		return prepareNativePackageUpdate(options);
	}

	const std::filesystem::path appDir = parentPath(options.appPath);
	if (appDir.empty()) {
		appendLog(options, L"Unable to resolve Mumble app directory.");
		return 2;
	}

	if (!directoryWritable(options, appDir)) {
		appendLog(options, L"App directory requires elevation; native package apply is disabled for this installation.");
		return ERROR_ACCESS_DENIED;
	}

	const PackagePlan pendingPlan = makePackagePlan(options);
	std::string pendingError;
	const std::filesystem::path statePath =
		Mumble::UpdateHealth::pendingStatePath(pendingPlan.updateRoot, pendingPlan.appPath);
	std::error_code stateFilesystemError;
	const bool stateExists = std::filesystem::exists(statePath, stateFilesystemError);
	auto pending = Mumble::UpdateHealth::readPendingState(pendingPlan.updateRoot, pendingPlan.appPath, &pendingError);
	if (!pending && (stateExists || stateFilesystemError)) {
		appendLog(options, L"Refusing a new update because the existing recovery journal is unreadable. "
						   + wideFromUtf8(pendingError));
		return ERROR_RECOVERY_FAILURE;
	}
	if (pending) {
		if (pending->state == Mumble::UpdateHealth::TransactionState::Committed
			|| pending->state == Mumble::UpdateHealth::TransactionState::RolledBack) {
			if (!persistTerminalTransaction(options, pendingPlan.updateRoot, *pending, pending->state, pendingError)) {
				return ERROR_RECOVERY_FAILURE;
			}
			appendLog(options, L"Finished cleanup for a terminal package transaction.");
		} else if (Mumble::UpdateHealth::markerConfirmsHealthy(pendingPlan.updateRoot, *pending, &pendingError)) {
			if (!persistTerminalTransaction(options, pendingPlan.updateRoot, *pending,
											Mumble::UpdateHealth::TransactionState::Committed, pendingError)) {
				return ERROR_RECOVERY_FAILURE;
			}
			appendLog(options, L"Finalized a previously healthy package update.");
		} else {
			appendLog(options, L"A previous package never reached its health marker; restoring it before this update.");
			if (!rollbackPendingPackage(options)) {
				return ERROR_RECOVERY_FAILURE;
			}
		}
	}

	return commitNativePackageUpdate(options);
}

bool rollbackPendingPackage(const Options &options) {
	const std::filesystem::path appPath(options.appPath);
	const std::filesystem::path appDir     = parentPath(options.appPath);
	const std::filesystem::path updateRoot = packageWorkRoot(options);
	std::string healthError;
	auto pending = Mumble::UpdateHealth::readPendingState(updateRoot, appPath, &healthError);
	if (!pending) {
		appendLog(options, L"No valid pending update rollback was available. " + wideFromUtf8(healthError));
		return false;
	}
	if (pending->state == Mumble::UpdateHealth::TransactionState::Committed
		|| pending->state == Mumble::UpdateHealth::TransactionState::RolledBack) {
		return persistTerminalTransaction(options, updateRoot, *pending, pending->state, healthError);
	}

	if (!pendingOwnsExpectedBackupRoot(updateRoot, *pending)) {
		appendLog(options, L"Refusing update rollback because its backup is not the transaction-owned snapshot.");
		return false;
	}

	std::vector< BackupEntry > backups;
	for (const Mumble::UpdateHealth::RollbackFile &file : pending->rollbackFiles) {
		BackupEntry backup;
		backup.relativePath = file.path;
		try {
			backup.target = resolvePackagePathUnderRoot(appDir, file.path);
			backup.backup = resolvePackagePathUnderRoot(pending->backupRoot, file.path);
		} catch (const std::exception &exception) {
			appendLog(options, L"Refusing invalid rollback file path. " + wideFromUtf8(exception.what()));
			return false;
		}
		backup.existed = file.existed;
		backup.size    = file.size;
		backup.sha256  = file.sha256;
		backups.push_back(std::move(backup));
	}

	try {
		restoreBackupEntries(backups);
		const std::filesystem::path currentManifest =
			updateRoot / L"installed-manifests" / (wideFromUtf8(appDirManifestKey(appDir)) + L".json");
		const std::filesystem::path manifestBackup = pending->backupRoot / L"installed-manifest.json";
		std::error_code error;
		if (pending->previousInstalledManifestExisted) {
			bool backupExists              = false;
			const std::uint64_t backupSize = fileSizeOrMissing(manifestBackup, backupExists);
			if (!backupExists || backupSize != pending->previousInstalledManifestSize
				|| fileSha256(manifestBackup) != pending->previousInstalledManifestSha256) {
				throw std::runtime_error("Known-good installed manifest failed verification.");
			}
			std::filesystem::create_directories(currentManifest.parent_path(), error);
			std::filesystem::copy_file(manifestBackup, currentManifest,
									   std::filesystem::copy_options::overwrite_existing, error);
			if (error || !flushFileDurably(currentManifest)
				|| fileSha256(currentManifest) != pending->previousInstalledManifestSha256) {
				throw std::runtime_error("Unable to restore the installed package manifest.");
			}
		} else {
			std::filesystem::remove(currentManifest, error);
			if (error || fileExists(currentManifest.wstring())) {
				throw std::runtime_error("Unable to remove the replacement installed package manifest.");
			}
		}

		if (!persistTerminalTransaction(options, updateRoot, *pending,
										Mumble::UpdateHealth::TransactionState::RolledBack, healthError)) {
			throw std::runtime_error("Unable to record terminal rollback state: " + healthError);
		}
		appendLog(options, L"Restored the last known-good immutable application payload.");
		return true;
	} catch (const std::exception &exception) {
		appendLog(options, L"Update rollback failed. " + wideFromUtf8(exception.what()));
		return false;
	}
}

struct RelaunchResult {
	bool launched  = false;
	HANDLE process = nullptr;
	DWORD error    = 0;
};

RelaunchResult relaunchMumble(const Options &options) {
	RelaunchResult result;
	if (!fileExists(options.appPath)) {
		appendLog(options, L"Mumble executable is missing after installation.");
		result.error = ERROR_FILE_NOT_FOUND;
		return result;
	}

	const std::wstring appPath = shellCompatiblePath(options.appPath);
	const std::wstring workingDirectory = shellCompatiblePath(options.workingDirectory);
	SHELLEXECUTEINFOW executeInfo{};
	executeInfo.cbSize      = sizeof(executeInfo);
	executeInfo.fMask       = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
	executeInfo.lpVerb      = L"open";
	executeInfo.lpFile      = appPath.c_str();
	executeInfo.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
	executeInfo.nShow       = SW_SHOWNORMAL;

	appendLog(options, L"Restarting Mumble.");
	if (!ShellExecuteExW(&executeInfo)) {
		result.error = GetLastError();
		appendLog(options, L"Failed to restart Mumble. Error " + std::to_wstring(result.error) + L'.');
		return result;
	}
	result.launched = true;
	result.process  = executeInfo.hProcess;
	return result;
}

bool waitForUpdateHealth(const Options &options, HANDLE process, Mumble::UpdateHealth::PendingUpdate pending) {
	const std::filesystem::path updateRoot = packageWorkRoot(options);
	std::string healthError;
	if (!process) {
		appendLog(options, L"Unable to monitor the restarted client; update health cannot be confirmed.");
		return false;
	}

	appendLog(options, L"Waiting for the restarted client to publish its stable audio health marker.");
	const ULONGLONG deadline = GetTickCount64() + pending.healthTimeoutMilliseconds;
	while (GetTickCount64() < deadline) {
		if (Mumble::UpdateHealth::markerConfirmsHealthy(updateRoot, pending, &healthError)) {
			if (!persistTerminalTransaction(options, updateRoot, pending,
											Mumble::UpdateHealth::TransactionState::Committed, healthError)) {
				appendLog(options, L"Unable to durably finalize the healthy transaction. " + wideFromUtf8(healthError));
				return false;
			}
			appendLog(options, L"Restarted client passed update health qualification.");
			return true;
		}
		const DWORD waitResult = WaitForSingleObject(process, 250);
		if (waitResult == WAIT_OBJECT_0) {
			appendLog(options, L"Restarted client exited before publishing its health marker.");
			return false;
		}
		if (waitResult == WAIT_FAILED) {
			appendLog(options, L"Unable to monitor the restarted client process.");
			return false;
		}
	}

	appendLog(options, L"Restarted client did not publish its health marker before the deadline.");
	return false;
}

DWORD runHealthQualifiedInstaller(const Options &options, bool *clientRelaunchHandled = nullptr) {
	if (clientRelaunchHandled) {
		*clientRelaunchHandled = false;
	}
	if (options.recoveryInstallerPath.empty()) {
		// Legacy manifests did not carry a known-good MSI. Preserve that path for
		// backward compatibility, but protocol-v4 channel pointers always supply
		// and verify a recovery installer before reaching this function.
		return runInstaller(options);
	}
	auto pending = armWindowsInstallerHealth(options);
	if (!pending) {
		return ERROR_RECOVERY_FAILURE;
	}

	const DWORD installerExitCode = runInstaller(options);
	if (!updateSucceeded(installerExitCode)) {
		appendLog(options, L"Candidate MSI failed; restoring the known-good MSI before returning.");
		return rollbackPendingWindowsInstaller(options, *pending) ? installerExitCode : ERROR_RECOVERY_FAILURE;
	}

	if (installerExitCode == RestartRequired) {
		// A 3010 result does not prove that the candidate executable is on disk;
		// Windows may still be running the previous image until reboot. Never
		// launch it into probation or allow it to publish the candidate marker.
		pending->restartRequired = true;
		std::string rebootStateError;
		if (!Mumble::UpdateHealth::writePendingState(packageWorkRoot(options), *pending, &rebootStateError)) {
			appendLog(options, L"Unable to persist reboot-required candidate state. "
							   + wideFromUtf8(rebootStateError));
		}
		appendLog(options, L"Candidate MSI returned 3010; failing closed and restoring known-good MSI without probation.");
		return rollbackPendingWindowsInstaller(options, *pending) ? RestartRequired : ERROR_RECOVERY_FAILURE;
	}

	pending->restartRequired = false;
	pending->state           = Mumble::UpdateHealth::TransactionState::AwaitingHealth;
	std::string healthError;
	if (!Mumble::UpdateHealth::writePendingState(packageWorkRoot(options), *pending, &healthError)) {
		appendLog(options, L"Unable to enter MSI health probation. " + wideFromUtf8(healthError));
		return rollbackPendingWindowsInstaller(options, *pending) ? ERROR_RECOVERY_FAILURE : ERROR_INSTALL_FAILURE;
	}
	if (options.noRelaunch) {
		appendLog(options, L"Protocol-v4 MSI health cannot succeed with --no-relaunch; restoring known-good MSI.");
		return rollbackPendingWindowsInstaller(options, *pending) ? ERROR_PROCESS_ABORTED : ERROR_RECOVERY_FAILURE;
	}

	appendLog(options, L"Candidate MSI installed; starting the 10-second update-health probation.");
	RelaunchResult restart = relaunchMumble(options);
	if (!restart.launched) {
		return rollbackPendingWindowsInstaller(options, *pending)
				 ? (restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error)
				 : ERROR_RECOVERY_FAILURE;
	}
	if (clientRelaunchHandled) {
		*clientRelaunchHandled = true;
	}
	const bool healthy = waitForUpdateHealth(options, restart.process, *pending);
	if (healthy) {
		CloseHandle(restart.process);
		return installerExitCode;
	}

	DWORD processExitCode = 0;
	if (GetExitCodeProcess(restart.process, &processExitCode) && processExitCode == STILL_ACTIVE) {
		appendLog(options, L"Stopping the unqualified MSI client before rollback.");
		TerminateProcess(restart.process, ERROR_PROCESS_ABORTED);
		WaitForSingleObject(restart.process, 5000);
	}
	CloseHandle(restart.process);
	if (!rollbackPendingWindowsInstaller(options, *pending)) {
		return ERROR_RECOVERY_FAILURE;
	}
	RelaunchResult restored = relaunchMumble(options);
	if (restored.process) {
		CloseHandle(restored.process);
	}
	if (!restored.launched) {
		return restored.error == 0 ? ERROR_PROCESS_ABORTED : restored.error;
	}
	if (clientRelaunchHandled) {
		*clientRelaunchHandled = true;
	}
	appendLog(options, L"Known-good MSI was restored and restarted after failed health qualification.");
	return ERROR_PROCESS_ABORTED;
}

DWORD restartPackageAndQualify(const Options &options) {
	PackagePlan plan = makePackagePlan(options);
	std::string healthError;
	try {
		const auto entries = readZipDirectory(plan.packagePath);
		const auto manifest = entries.find("manifest.json");
		if (manifest == entries.end()) {
			appendLog(options, L"Verified update package lost manifest.json before restart qualification.");
			return ERROR_INVALID_DATA;
		}
		parsePackageManifest(readZipEntryText(plan.packagePath, manifest->second), plan);
	} catch (const std::exception &exception) {
		appendLog(options, L"Unable to recover the package health contract. " + wideFromUtf8(exception.what()));
		return ERROR_INVALID_DATA;
	}

	const std::filesystem::path statePath = Mumble::UpdateHealth::pendingStatePath(plan.updateRoot, plan.appPath);
	std::error_code stateFilesystemError;
	const bool stateExists = std::filesystem::exists(statePath, stateFilesystemError);
	auto pending = Mumble::UpdateHealth::readPendingState(plan.updateRoot, plan.appPath, &healthError);
	if (plan.healthCheckRequired
		&& (!pending || pending->state != Mumble::UpdateHealth::TransactionState::AwaitingHealth)) {
		appendLog(options, L"Required package health journal is missing or invalid; refusing fail-open restart. "
						   + wideFromUtf8(healthError));
		return ERROR_RECOVERY_FAILURE;
	}
	if (!pending && (stateExists || stateFilesystemError)) {
		appendLog(options, L"Package health journal exists but is unreadable; refusing fail-open restart. "
						   + wideFromUtf8(healthError));
		return ERROR_RECOVERY_FAILURE;
	}
	const bool healthRequired = pending.has_value();

	RelaunchResult restart = relaunchMumble(options);
	if (!restart.launched) {
		if (healthRequired && !rollbackPendingPackage(options)) {
			return ERROR_RECOVERY_FAILURE;
		}
		return restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error;
	}

	if (!healthRequired) {
		if (restart.process) {
			CloseHandle(restart.process);
		}
		return 0;
	}

	const bool healthy = waitForUpdateHealth(options, restart.process, *pending);
	if (healthy) {
		CloseHandle(restart.process);
		return 0;
	}

	DWORD processExitCode = 0;
	if (GetExitCodeProcess(restart.process, &processExitCode) && processExitCode == STILL_ACTIVE) {
		appendLog(options, L"Stopping the unqualified client before rollback.");
		TerminateProcess(restart.process, ERROR_PROCESS_ABORTED);
		WaitForSingleObject(restart.process, 5000);
	}
	CloseHandle(restart.process);

	if (!rollbackPendingPackage(options)) {
		return ERROR_RECOVERY_FAILURE;
	}

	RelaunchResult restored = relaunchMumble(options);
	if (restored.process) {
		CloseHandle(restored.process);
	}
	if (!restored.launched) {
		appendLog(options, L"Known-good payload was restored but could not be restarted.");
		return restored.error == 0 ? ERROR_PROCESS_ABORTED : restored.error;
	}
	appendLog(options, L"Known-good payload was restored and restarted after failed health qualification.");
	return ERROR_PROCESS_ABORTED;
}

DWORD runPendingRecovery(const Options &options) {
	appendLog(options, L"Starting persistent update recovery.");
	try {
		if (processIsElevated()) {
			appendLog(options, L"Refusing user-state package recovery from an elevated token.");
			return ERROR_ELEVATION_REQUIRED;
		}
		if (options.parentPid != 0) {
			appendLog(options, L"Recovery watchdog is waiting for the owning updater to exit.");
			constexpr ULONGLONG maximumParentWaitMilliseconds = 10ULL * 60ULL * 1000ULL;
			const ULONGLONG deadline                          = GetTickCount64() + maximumParentWaitMilliseconds;
			while (GetTickCount64() < deadline) {
				HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, options.parentPid);
				if (!parent) {
					break;
				}
				DWORD exitCode    = 0;
				const bool active = GetExitCodeProcess(parent, &exitCode) && exitCode == STILL_ACTIVE;
				CloseHandle(parent);
				if (!active) {
					break;
				}
				Sleep(100);
			}
			HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, options.parentPid);
			if (parent) {
				DWORD exitCode         = 0;
				const bool stillActive = GetExitCodeProcess(parent, &exitCode) && exitCode == STILL_ACTIVE;
				CloseHandle(parent);
				if (stillActive) {
					appendLog(options, L"Recovery watchdog timed out without touching the active update transaction.");
					return ERROR_TIMEOUT;
				}
			}
		}
		if (!armRecoveryBootstrap(options)) {
			appendLog(options, L"Unable to re-arm persistent update recovery.");
			return ERROR_RECOVERY_FAILURE;
		}

		const std::filesystem::path appPath(options.appPath);
		const std::filesystem::path updateRoot = packageWorkRoot(options);
		const std::filesystem::path statePath  = Mumble::UpdateHealth::pendingStatePath(updateRoot, appPath);
		std::error_code filesystemError;
		const bool stateExists = std::filesystem::exists(statePath, filesystemError);
		std::string healthError;
		auto pending = Mumble::UpdateHealth::readPendingState(updateRoot, appPath, &healthError);
		if (!pending) {
			if (stateExists || filesystemError) {
				appendLog(options,
						  L"Persistent recovery found an unreadable pending journal. " + wideFromUtf8(healthError));
				return ERROR_RECOVERY_FAILURE;
			}
			clearRecoveryRunOnce(options);
			appendLog(options, L"No pending update transaction remains.");
			return 0;
		}
		if (pending->state == Mumble::UpdateHealth::TransactionState::Committed
			|| pending->state == Mumble::UpdateHealth::TransactionState::RolledBack) {
			const auto terminalState = pending->state;
			if (!persistTerminalTransaction(options, updateRoot, *pending, terminalState, healthError)) {
				appendLog(options, L"Unable to finish terminal transaction cleanup. " + wideFromUtf8(healthError));
				return ERROR_RECOVERY_FAILURE;
			}
			appendLog(options, terminalState == Mumble::UpdateHealth::TransactionState::Committed
								   ? L"Persistent recovery completed a committed transaction cleanup."
								   : L"Persistent recovery completed a rolled-back transaction cleanup.");
			if (options.noRelaunch) {
				return 0;
			}
			RelaunchResult restart = relaunchMumble(options);
			if (restart.process) {
				CloseHandle(restart.process);
			}
			return restart.launched ? 0 : (restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error);
		}

		if (Mumble::UpdateHealth::markerConfirmsHealthy(updateRoot, *pending, &healthError)) {
			if (!persistTerminalTransaction(options, updateRoot, *pending,
											Mumble::UpdateHealth::TransactionState::Committed, healthError)) {
				appendLog(options, L"Unable to finalize the healthy pending update. " + wideFromUtf8(healthError));
				return ERROR_RECOVERY_FAILURE;
			}
			appendLog(options, L"Recovered updater state by accepting the durable client health marker.");
			if (options.noRelaunch) {
				return 0;
			}
			RelaunchResult restart = relaunchMumble(options);
			if (restart.process) {
				CloseHandle(restart.process);
			}
			return restart.launched ? 0 : (restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error);
		}

		const std::filesystem::path appDir = parentPath(options.appPath);
		if (pending->mode == Mumble::UpdateHealth::TransactionMode::NativePackage
			&& !directoryWritable(options, appDir)) {
			appendLog(options, L"Persistent native recovery cannot write this installation; use MSI repair.");
			return ERROR_ACCESS_DENIED;
		}

		appendLog(options, L"The interrupted update never qualified; rolling back from the durable journal.");
		if (!rollbackPendingUpdate(options)) {
			return ERROR_RECOVERY_FAILURE;
		}
		if (options.noRelaunch) {
			return 0;
		}
		RelaunchResult restart = relaunchMumble(options);
		if (restart.process) {
			CloseHandle(restart.process);
		}
		if (!restart.launched) {
			appendLog(options, L"Known-good payload was restored but could not be restarted by persistent recovery.");
			return restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error;
		}
		appendLog(options, L"Persistent recovery restored and restarted the known-good payload.");
		return 0;
	} catch (const std::exception &exception) {
		appendLog(options, L"Persistent update recovery failed. " + wideFromUtf8(exception.what()));
		return ERROR_RECOVERY_FAILURE;
	}
}

constexpr UINT UiStatusMessage            = WM_APP + 1;
constexpr UINT UiProgressMessage          = WM_APP + 2;
constexpr UINT UiDoneMessage              = WM_APP + 3;
constexpr UINT_PTR UiRefreshTimer         = 1;
constexpr UINT_PTR UiAutoCloseTimer       = 2;
constexpr DWORD UiRefreshIntervalMsec     = 350;
constexpr DWORD UiAutoCloseDelayMsec      = 1800;
constexpr std::uintmax_t MaxLogTailBytes  = 256 * 1024;
constexpr int MumbleUpdaterIconResourceId = 101;

enum : int { ControlTitle = 1001, ControlStatus, ControlDetails, ControlClose, ControlLog };

struct UiProgressPayload {
	int percent        = 0;
	bool indeterminate = true;
};

std::atomic< HWND > g_updaterWindow{ nullptr };

struct UpdaterTheme {
	COLORREF crust         = RGB(25, 31, 38);
	COLORREF mantle        = RGB(37, 44, 52);
	COLORREF base          = RGB(25, 31, 38);
	COLORREF surface0      = RGB(49, 58, 68);
	COLORREF surface1      = RGB(57, 66, 77);
	COLORREF surface2      = RGB(52, 61, 72);
	COLORREF text          = RGB(224, 231, 239);
	COLORREF subtext0      = RGB(125, 137, 150);
	COLORREF overlay0      = RGB(125, 137, 150);
	COLORREF accent        = RGB(106, 166, 207);
	COLORREF accentHover   = RGB(130, 193, 224);
	COLORREF success       = RGB(105, 178, 140);
	COLORREF warning       = RGB(199, 146, 91);
	COLORREF danger        = RGB(196, 106, 116);
	COLORREF onAccent      = RGB(25, 31, 38);
	COLORREF caption       = RGB(25, 31, 38);
	COLORREF captionText   = RGB(224, 231, 239);
	COLORREF captionBorder = RGB(57, 66, 77);
	bool dark              = true;
};

int colorRed(const COLORREF color) {
	return GetRValue(color);
}
int colorGreen(const COLORREF color) {
	return GetGValue(color);
}
int colorBlue(const COLORREF color) {
	return GetBValue(color);
}

COLORREF mixColors(const COLORREF base, const COLORREF overlay, const double overlayRatio) {
	const double clampedRatio = std::clamp(overlayRatio, 0.0, 1.0);
	const double baseRatio    = 1.0 - clampedRatio;
	return RGB(static_cast< int >(colorRed(base) * baseRatio + colorRed(overlay) * clampedRatio),
			   static_cast< int >(colorGreen(base) * baseRatio + colorGreen(overlay) * clampedRatio),
			   static_cast< int >(colorBlue(base) * baseRatio + colorBlue(overlay) * clampedRatio));
}

int colorLightness(const COLORREF color) {
	const int maxComponent = std::max({ colorRed(color), colorGreen(color), colorBlue(color) });
	const int minComponent = std::min({ colorRed(color), colorGreen(color), colorBlue(color) });
	return (maxComponent + minComponent) / 2;
}

bool parseBoolean(const std::wstring &value, const bool fallback) {
	std::wstring normalized;
	normalized.reserve(value.size());
	for (const wchar_t ch : value) {
		normalized.push_back(static_cast< wchar_t >(std::towlower(ch)));
	}
	if (normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on") {
		return true;
	}
	if (normalized == L"0" || normalized == L"false" || normalized == L"no" || normalized == L"off") {
		return false;
	}
	return fallback;
}

bool parseHexColor(std::wstring value, COLORREF &color) {
	value.erase(std::remove_if(value.begin(), value.end(), [](const wchar_t ch) { return std::iswspace(ch); }),
				value.end());
	if (!value.empty() && value.front() == L'#') {
		value.erase(value.begin());
	}
	if (value.size() != 6) {
		return false;
	}
	for (const wchar_t ch : value) {
		if (!std::iswxdigit(ch)) {
			return false;
		}
	}

	const unsigned long raw = std::wcstoul(value.c_str(), nullptr, 16);
	color                   = RGB(static_cast< int >((raw >> 16) & 0xff), static_cast< int >((raw >> 8) & 0xff),
								  static_cast< int >(raw & 0xff));
	return true;
}

std::wstring lowerAscii(std::wstring value) {
	for (wchar_t &ch : value) {
		if (ch >= L'A' && ch <= L'Z') {
			ch = static_cast< wchar_t >(ch - L'A' + L'a');
		}
	}
	return value;
}

UpdaterTheme themeFromOptions(const Options &options) {
	UpdaterTheme theme;
	std::size_t start = 0;
	while (start <= options.uiTheme.size()) {
		const std::size_t end = options.uiTheme.find(L';', start);
		const std::wstring entry =
			options.uiTheme.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
		const std::size_t separator = entry.find(L'=');
		if (separator != std::wstring::npos) {
			const std::wstring key   = lowerAscii(entry.substr(0, separator));
			const std::wstring value = entry.substr(separator + 1);
			COLORREF parsedColor     = 0;
			if (key == L"dark") {
				theme.dark = parseBoolean(value, theme.dark);
			} else if (parseHexColor(value, parsedColor)) {
				if (key == L"crust") {
					theme.crust = parsedColor;
				} else if (key == L"mantle") {
					theme.mantle = parsedColor;
				} else if (key == L"base") {
					theme.base = parsedColor;
				} else if (key == L"surface0") {
					theme.surface0 = parsedColor;
				} else if (key == L"surface1") {
					theme.surface1 = parsedColor;
				} else if (key == L"surface2") {
					theme.surface2 = parsedColor;
				} else if (key == L"text") {
					theme.text = parsedColor;
				} else if (key == L"subtext0") {
					theme.subtext0 = parsedColor;
				} else if (key == L"overlay0") {
					theme.overlay0 = parsedColor;
				} else if (key == L"accent") {
					theme.accent = parsedColor;
				} else if (key == L"accenthover") {
					theme.accentHover = parsedColor;
				} else if (key == L"success") {
					theme.success = parsedColor;
				} else if (key == L"warning") {
					theme.warning = parsedColor;
				} else if (key == L"danger") {
					theme.danger = parsedColor;
				} else if (key == L"onaccent") {
					theme.onAccent = parsedColor;
				} else if (key == L"caption") {
					theme.caption = parsedColor;
				} else if (key == L"captiontext") {
					theme.captionText = parsedColor;
				} else if (key == L"captionborder") {
					theme.captionBorder = parsedColor;
				}
			}
		}

		if (end == std::wstring::npos) {
			break;
		}
		start = end + 1;
	}

	if (options.uiTheme.empty()) {
		theme.dark = colorLightness(theme.text) > colorLightness(theme.crust);
	}
	return theme;
}

std::wstring wideFromBytes(const char *data, const int size, const UINT codePage) {
	if (!data || size <= 0) {
		return {};
	}

	const int charCount = MultiByteToWideChar(codePage, 0, data, size, nullptr, 0);
	if (charCount <= 0) {
		return {};
	}

	std::wstring result(static_cast< std::size_t >(charCount), L'\0');
	MultiByteToWideChar(codePage, 0, data, size, result.data(), charCount);
	if (!result.empty() && result.front() == 0xfeff) {
		result.erase(result.begin());
	}
	return result;
}

std::wstring decodeTextBytes(const std::vector< char > &bytes) {
	if (bytes.empty()) {
		return {};
	}

	const unsigned char *raw     = reinterpret_cast< const unsigned char * >(bytes.data());
	const bool hasUtf16LeBom     = bytes.size() >= 2 && raw[0] == 0xff && raw[1] == 0xfe;
	const std::size_t sampleSize = std::min< std::size_t >(bytes.size(), 4096);
	std::size_t evenNuls         = 0;
	std::size_t oddNuls          = 0;
	for (std::size_t index = 0; index < sampleSize; ++index) {
		if (raw[index] == 0) {
			if (index % 2 == 0) {
				++evenNuls;
			} else {
				++oddNuls;
			}
		}
	}

	const bool looksUtf16Le = hasUtf16LeBom || (oddNuls > sampleSize / 8 && oddNuls > evenNuls * 2);
	if (looksUtf16Le) {
		std::size_t start = hasUtf16LeBom ? 2 : 0;
		std::size_t count = (bytes.size() - start) / 2;
		std::wstring result;
		result.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const std::size_t byteIndex = start + index * 2;
			const wchar_t ch            = static_cast< wchar_t >(static_cast< unsigned char >(bytes[byteIndex])
																 | (static_cast< unsigned char >(bytes[byteIndex + 1]) << 8));
			result.push_back(ch);
		}
		return result;
	}

	std::wstring result = wideFromBytes(bytes.data(), static_cast< int >(bytes.size()), CP_UTF8);
	if (!result.empty()) {
		return result;
	}
	return wideFromBytes(bytes.data(), static_cast< int >(bytes.size()), CP_ACP);
}

std::wstring normalizeLineEndings(const std::wstring &text) {
	std::wstring result;
	result.reserve(text.size() + 64);
	for (std::size_t index = 0; index < text.size(); ++index) {
		const wchar_t ch = text[index];
		if (ch == L'\r') {
			result.push_back(L'\r');
			if (index + 1 < text.size() && text[index + 1] == L'\n') {
				result.push_back(L'\n');
				++index;
			} else {
				result.push_back(L'\n');
			}
		} else if (ch == L'\n') {
			result.append(L"\r\n");
		} else {
			result.push_back(ch);
		}
	}
	return result;
}

std::wstring readFileTail(const std::wstring &path) {
	if (path.empty()) {
		return {};
	}

	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}

	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
		CloseHandle(file);
		return {};
	}

	std::uintmax_t start = 0;
	if (static_cast< std::uintmax_t >(size.QuadPart) > MaxLogTailBytes) {
		start = static_cast< std::uintmax_t >(size.QuadPart) - MaxLogTailBytes;
		if (start % 2 != 0) {
			--start;
		}
	}

	LARGE_INTEGER distance{};
	distance.QuadPart = static_cast< LONGLONG >(start);
	if (!SetFilePointerEx(file, distance, nullptr, FILE_BEGIN)) {
		CloseHandle(file);
		return {};
	}

	const DWORD bytesToRead = static_cast< DWORD >(
		std::min< std::uintmax_t >(MaxLogTailBytes, static_cast< std::uintmax_t >(size.QuadPart) - start));
	std::vector< char > buffer(bytesToRead);
	DWORD bytesRead   = 0;
	const BOOL readOk = ReadFile(file, buffer.data(), bytesToRead, &bytesRead, nullptr);
	CloseHandle(file);
	if (!readOk || bytesRead == 0) {
		return {};
	}
	buffer.resize(bytesRead);

	std::wstring text = decodeTextBytes(buffer);
	if (start > 0) {
		const std::size_t firstLineEnd = text.find(L'\n');
		if (firstLineEnd != std::wstring::npos) {
			text.erase(0, firstLineEnd + 1);
		}
		text.insert(0, L"...\n");
	}
	return normalizeLineEndings(text);
}

std::wstring fileNameForDisplay(const std::wstring &path) {
	if (path.empty()) {
		return {};
	}

	const std::filesystem::path filePath(path);
	std::wstring name = filePath.filename().wstring();
	if (!name.empty()) {
		return name;
	}
	return path;
}

std::wstring lastLogMessage(const std::wstring &text) {
	std::size_t end = text.find_last_not_of(L"\r\n\t ");
	if (end == std::wstring::npos) {
		return {};
	}

	std::size_t start        = text.find_last_of(L"\r\n", end);
	start                    = start == std::wstring::npos ? 0 : start + 1;
	std::wstring line        = text.substr(start, end - start + 1);
	const std::size_t marker = line.find(L"] ");
	if (!line.empty() && line.front() == L'[' && marker != std::wstring::npos) {
		line.erase(0, marker + 2);
	}
	return line;
}

bool parsePackageProgress(const std::wstring &text, int &percent) {
	const std::size_t marker = text.rfind(L"Progress ");
	if (marker == std::wstring::npos) {
		return false;
	}

	const wchar_t *cursor = text.c_str() + marker + 9;
	wchar_t *end          = nullptr;
	const long current    = std::wcstol(cursor, &end, 10);
	if (!end || *end != L'/') {
		return false;
	}
	cursor           = end + 1;
	const long total = std::wcstol(cursor, &end, 10);
	if (total <= 0 || current < 0) {
		return false;
	}

	percent = static_cast< int >(std::clamp((current * 100) / total, 0L, 100L));
	return true;
}

class UpdaterProgressWindow {
public:
	explicit UpdaterProgressWindow(const Options &options) : m_options(options), m_theme(themeFromOptions(options)) {}

	bool create(HINSTANCE instance) {
		HICON largeIcon = reinterpret_cast< HICON >(LoadImageW(instance, MAKEINTRESOURCEW(MumbleUpdaterIconResourceId),
															   IMAGE_ICON, GetSystemMetrics(SM_CXICON),
															   GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
		HICON smallIcon = reinterpret_cast< HICON >(LoadImageW(instance, MAKEINTRESOURCEW(MumbleUpdaterIconResourceId),
															   IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
															   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
		if (!largeIcon) {
			largeIcon = LoadIconW(nullptr, IDI_APPLICATION);
		}
		if (!smallIcon) {
			smallIcon = largeIcon;
		}

		WNDCLASSEXW windowClass{};
		windowClass.cbSize        = sizeof(windowClass);
		windowClass.lpfnWndProc   = &UpdaterProgressWindow::windowProc;
		windowClass.hInstance     = instance;
		windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hIcon         = largeIcon;
		windowClass.hIconSm       = smallIcon;
		windowClass.hbrBackground = nullptr;
		windowClass.lpszClassName = L"MumbleUpdaterProgressWindow";
		RegisterClassExW(&windowClass);

		const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		RECT rect{ 0, 0, collapsedWidth(), collapsedHeight() };
		AdjustWindowRectEx(&rect, style, FALSE, 0);

		m_hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"Mumble update", style, CW_USEDEFAULT, CW_USEDEFAULT,
								 rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
		if (!m_hwnd) {
			return false;
		}

		applyNativeTitleBar();
		g_updaterWindow.store(m_hwnd);
		ShowWindow(m_hwnd, SW_SHOWNORMAL);
		UpdateWindow(m_hwnd);
		return true;
	}

	int run() {
		MSG message{};
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		g_updaterWindow.store(nullptr);
		return m_exitCode;
	}

private:
	Options m_options;
	UpdaterTheme m_theme;
	HWND m_hwnd          = nullptr;
	HWND m_title         = nullptr;
	HWND m_status        = nullptr;
	HWND m_detailsButton = nullptr;
	HWND m_closeButton   = nullptr;
	HWND m_log           = nullptr;
	RECT m_badgeRect{};
	RECT m_progressRect{};
	HFONT m_uiFont           = nullptr;
	HFONT m_titleFont        = nullptr;
	HFONT m_logFont          = nullptr;
	HBRUSH m_backgroundBrush = nullptr;
	HBRUSH m_panelBrush      = nullptr;
	HBRUSH m_logBrush        = nullptr;
	bool m_detailsVisible    = false;
	bool m_completed         = false;
	bool m_indeterminate     = true;
	int m_exitCode           = 1;
	int m_progressPercent    = 0;
	int m_activityFrame      = 0;
	std::wstring m_lastLogText;
	std::wstring m_statusText = L"Preparing update...";

	int collapsedWidth() const { return 560; }
	int collapsedHeight() const { return 180; }
	int expandedHeight() const { return 500; }

	static constexpr DWORD DwmUseImmersiveDarkModeLegacyAttribute = 19;
	static constexpr DWORD DwmUseImmersiveDarkModeAttribute       = 20;
	static constexpr DWORD DwmBorderColorAttribute                = 34;
	static constexpr DWORD DwmCaptionColorAttribute               = 35;
	static constexpr DWORD DwmTextColorAttribute                  = 36;

	void applyNativeTitleBar() const {
		if (!m_hwnd) {
			return;
		}

		const BOOL immersiveDarkMode = m_theme.dark ? TRUE : FALSE;
		HRESULT result = DwmSetWindowAttribute(m_hwnd, DwmUseImmersiveDarkModeAttribute, &immersiveDarkMode,
											   sizeof(immersiveDarkMode));
		if (FAILED(result)) {
			DwmSetWindowAttribute(m_hwnd, DwmUseImmersiveDarkModeLegacyAttribute, &immersiveDarkMode,
								  sizeof(immersiveDarkMode));
		}

		DwmSetWindowAttribute(m_hwnd, DwmCaptionColorAttribute, &m_theme.caption, sizeof(m_theme.caption));
		DwmSetWindowAttribute(m_hwnd, DwmTextColorAttribute, &m_theme.captionText, sizeof(m_theme.captionText));
		DwmSetWindowAttribute(m_hwnd, DwmBorderColorAttribute, &m_theme.captionBorder, sizeof(m_theme.captionBorder));
	}

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		UpdaterProgressWindow *self = nullptr;
		if (message == WM_NCCREATE) {
			auto *create = reinterpret_cast< CREATESTRUCTW * >(lParam);
			self         = static_cast< UpdaterProgressWindow * >(create->lpCreateParams);
			self->m_hwnd = hwnd;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >(self));
		} else {
			self = reinterpret_cast< UpdaterProgressWindow * >(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
	}

	LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
		switch (message) {
			case WM_CREATE:
				createControls();
				SetTimer(m_hwnd, UiRefreshTimer, UiRefreshIntervalMsec, nullptr);
				setProgress(-1, true);
				refreshLogs();
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
				paintWindow();
				return 0;
			case WM_SIZE:
				layoutControls();
				InvalidateRect(m_hwnd, nullptr, TRUE);
				return 0;
			case WM_CTLCOLORSTATIC:
				return handleControlColor(reinterpret_cast< HDC >(wParam), reinterpret_cast< HWND >(lParam));
			case WM_CTLCOLOREDIT:
				return handleEditColor(reinterpret_cast< HDC >(wParam));
			case WM_DRAWITEM:
				if (drawButton(reinterpret_cast< const DRAWITEMSTRUCT * >(lParam))) {
					return TRUE;
				}
				break;
			case WM_TIMER:
				if (wParam == UiRefreshTimer) {
					advanceSpinner();
					refreshLogs();
				} else if (wParam == UiAutoCloseTimer) {
					DestroyWindow(m_hwnd);
				}
				return 0;
			case WM_COMMAND:
				if (LOWORD(wParam) == ControlDetails) {
					toggleDetails();
					return 0;
				}
				if (LOWORD(wParam) == ControlClose && m_completed) {
					DestroyWindow(m_hwnd);
					return 0;
				}
				break;
			case UiStatusMessage: {
				std::unique_ptr< std::wstring > text(reinterpret_cast< std::wstring * >(lParam));
				if (text && !text->empty()) {
					setStatus(*text);
				}
				return 0;
			}
			case UiProgressMessage: {
				std::unique_ptr< UiProgressPayload > payload(reinterpret_cast< UiProgressPayload * >(lParam));
				if (payload) {
					setProgress(payload->percent, payload->indeterminate);
				}
				return 0;
			}
			case UiDoneMessage:
				finish(static_cast< DWORD >(wParam));
				return 0;
			case WM_CLOSE:
				if (m_completed) {
					DestroyWindow(m_hwnd);
				} else {
					MessageBeep(MB_ICONINFORMATION);
				}
				return 0;
			case WM_DESTROY:
				KillTimer(m_hwnd, UiRefreshTimer);
				KillTimer(m_hwnd, UiAutoCloseTimer);
				if (m_logFont) {
					DeleteObject(m_logFont);
				}
				if (m_uiFont) {
					DeleteObject(m_uiFont);
				}
				if (m_titleFont) {
					DeleteObject(m_titleFont);
				}
				if (m_backgroundBrush) {
					DeleteObject(m_backgroundBrush);
				}
				if (m_panelBrush) {
					DeleteObject(m_panelBrush);
				}
				if (m_logBrush) {
					DeleteObject(m_logBrush);
				}
				if (m_hwnd == g_updaterWindow.load()) {
					g_updaterWindow.store(nullptr);
				}
				PostQuitMessage(0);
				return 0;
		}

		return DefWindowProcW(m_hwnd, message, wParam, lParam);
	}

	void fillRectColor(HDC hdc, const RECT &rect, const COLORREF color) const {
		HBRUSH brush = CreateSolidBrush(color);
		FillRect(hdc, &rect, brush);
		DeleteObject(brush);
	}

	void drawRoundedRect(HDC hdc, const RECT &rect, const COLORREF fill, const COLORREF border,
						 const int radius) const {
		HBRUSH brush           = CreateSolidBrush(fill);
		HPEN pen               = CreatePen(PS_SOLID, 1, border);
		const HGDIOBJ oldBrush = SelectObject(hdc, brush);
		const HGDIOBJ oldPen   = SelectObject(hdc, pen);
		RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
		SelectObject(hdc, oldBrush);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		DeleteObject(brush);
	}

	void drawFilledEllipse(HDC hdc, const RECT &rect, const COLORREF fill, const COLORREF border) const {
		HBRUSH brush           = CreateSolidBrush(fill);
		HPEN pen               = CreatePen(PS_SOLID, 1, border);
		const HGDIOBJ oldBrush = SelectObject(hdc, brush);
		const HGDIOBJ oldPen   = SelectObject(hdc, pen);
		Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);
		SelectObject(hdc, oldBrush);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		DeleteObject(brush);
	}

	void drawStatusBadgeGlyph(HDC hdc, const RECT &badge) const {
		if (!m_completed) {
			const int dotSize    = 5;
			const int gap        = 4;
			const int totalWidth = dotSize * 3 + gap * 2;
			const int left       = badge.left + ((badge.right - badge.left - totalWidth) / 2);
			const int top        = badge.top + ((badge.bottom - badge.top - dotSize) / 2);
			for (int index = 0; index < 3; ++index) {
				const bool active = index == (m_activityFrame % 3);
				const RECT dot{ left + index * (dotSize + gap), top, left + index * (dotSize + gap) + dotSize,
								top + dotSize };
				const COLORREF dotColor =
					active ? m_theme.accentHover : mixColors(m_theme.surface2, m_theme.accent, 0.22);
				drawFilledEllipse(hdc, dot, dotColor, dotColor);
			}
			return;
		}

		const bool success = m_exitCode == 0 || static_cast< DWORD >(m_exitCode) == RestartRequired;
		if (success) {
			HPEN pen             = CreatePen(PS_SOLID, 3, m_theme.success);
			const HGDIOBJ oldPen = SelectObject(hdc, pen);
			MoveToEx(hdc, badge.left + 9, badge.top + 17, nullptr);
			LineTo(hdc, badge.left + 14, badge.top + 22);
			LineTo(hdc, badge.left + 24, badge.top + 10);
			SelectObject(hdc, oldPen);
			DeleteObject(pen);
			return;
		}

		HPEN pen             = CreatePen(PS_SOLID, 3, m_theme.danger);
		const HGDIOBJ oldPen = SelectObject(hdc, pen);
		MoveToEx(hdc, badge.left + 16, badge.top + 9, nullptr);
		LineTo(hdc, badge.left + 16, badge.top + 19);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		const RECT dot{ badge.left + 14, badge.top + 23, badge.left + 18, badge.top + 27 };
		drawFilledEllipse(hdc, dot, m_theme.danger, m_theme.danger);
	}

	void paintWindow() {
		PAINTSTRUCT paint{};
		HDC hdc = BeginPaint(m_hwnd, &paint);
		RECT client{};
		GetClientRect(m_hwnd, &client);
		FillRect(hdc, &client, m_backgroundBrush);

		RECT panel{ 12, 12, client.right - 12, client.bottom - 12 };
		drawRoundedRect(hdc, panel, m_theme.base, mixColors(m_theme.surface1, m_theme.text, 0.08), 14);

		RECT accentStrip{ panel.left, panel.top + 8, panel.left + 3, panel.bottom - 8 };
		fillRectColor(hdc, accentStrip, m_theme.accent);

		const int buttonTop = m_detailsVisible ? expandedHeight() - 56 : collapsedHeight() - 56;
		RECT actionDivider{ panel.left + 1, buttonTop - 13, panel.right - 1, buttonTop - 12 };
		fillRectColor(hdc, actionDivider, mixColors(m_theme.surface1, m_theme.text, 0.045));

		RECT badge               = m_badgeRect;
		const COLORREF badgeFill = m_completed ? mixColors(m_theme.surface0, m_theme.success, 0.18)
											   : mixColors(m_theme.surface0, m_theme.accent, 0.16);
		const COLORREF badgeBorder =
			m_completed
				? mixColors(m_theme.surface1,
							(m_exitCode == 0 || static_cast< DWORD >(m_exitCode) == RestartRequired) ? m_theme.success
																									 : m_theme.danger,
							0.42)
				: mixColors(m_theme.surface1, m_theme.accent, 0.34);
		drawRoundedRect(hdc, badge, badgeFill, badgeBorder, 16);
		drawStatusBadgeGlyph(hdc, badge);
		paintProgress(hdc);

		if (m_detailsVisible) {
			RECT logRect{};
			GetWindowRect(m_log, &logRect);
			MapWindowPoints(nullptr, m_hwnd, reinterpret_cast< POINT * >(&logRect), 2);
			RECT frame{ logRect.left - 1, logRect.top - 1, logRect.right + 1, logRect.bottom + 1 };
			drawRoundedRect(hdc, frame, m_theme.crust, mixColors(m_theme.surface1, m_theme.text, 0.06), 8);
		}

		EndPaint(m_hwnd, &paint);
	}

	void paintProgress(HDC hdc) const {
		RECT track = m_progressRect;
		if (track.right <= track.left || track.bottom <= track.top) {
			return;
		}

		drawRoundedRect(hdc, track, m_theme.surface0, mixColors(m_theme.surface1, m_theme.text, 0.08), 7);
		InflateRect(&track, -2, -2);
		if (track.right <= track.left || track.bottom <= track.top) {
			return;
		}

		RECT fill            = track;
		const int trackWidth = track.right - track.left;
		if (m_indeterminate && !m_completed) {
			const int pulseWidth = std::max(70, trackWidth / 3);
			const int travel     = trackWidth + pulseWidth;
			const int offset     = (m_activityFrame * 22) % std::max(1, travel);
			fill.left            = track.left + offset - pulseWidth;
			fill.right           = fill.left + pulseWidth;
			fill.left            = std::max(fill.left, track.left);
			fill.right           = std::min(fill.right, track.right);
			if (fill.right <= fill.left) {
				return;
			}
		} else {
			fill.right = track.left + ((trackWidth * std::clamp(m_progressPercent, 0, 100)) / 100);
			if (fill.right <= fill.left) {
				return;
			}
		}

		const COLORREF fillColor = m_completed && m_exitCode != 0 && static_cast< DWORD >(m_exitCode) != RestartRequired
									   ? m_theme.danger
									   : m_theme.accent;
		drawRoundedRect(hdc, fill, mixColors(fillColor, m_theme.accentHover, 0.16), fillColor, 5);
	}

	LRESULT handleControlColor(HDC hdc, HWND control) const {
		SetBkMode(hdc, TRANSPARENT);
		if (control == m_title) {
			SetTextColor(hdc, m_theme.text);
			return reinterpret_cast< LRESULT >(m_panelBrush);
		}
		SetTextColor(hdc, m_theme.subtext0);
		return reinterpret_cast< LRESULT >(m_panelBrush);
	}

	LRESULT handleEditColor(HDC hdc) const {
		SetBkMode(hdc, OPAQUE);
		SetBkColor(hdc, m_theme.crust);
		SetTextColor(hdc, m_theme.subtext0);
		return reinterpret_cast< LRESULT >(m_logBrush);
	}

	bool drawButton(const DRAWITEMSTRUCT *item) const {
		if (!item || (item->CtlID != ControlDetails && item->CtlID != ControlClose)) {
			return false;
		}

		const bool disabled = (item->itemState & ODS_DISABLED) != 0;
		const bool pressed  = (item->itemState & ODS_SELECTED) != 0;
		const bool focused  = (item->itemState & ODS_FOCUS) != 0;
		const bool primary  = item->CtlID == ControlClose && m_completed && !disabled;
		COLORREF fill       = primary ? m_theme.accent : m_theme.surface0;
		if (pressed) {
			fill = primary ? mixColors(m_theme.accent, m_theme.crust, 0.16)
						   : mixColors(m_theme.surface0, m_theme.text, 0.08);
		}
		if (disabled) {
			fill = mixColors(m_theme.base, m_theme.surface0, 0.54);
		}
		const COLORREF border = focused ? m_theme.accent
										: (primary ? mixColors(m_theme.accent, m_theme.text, 0.12)
												   : mixColors(m_theme.surface1, m_theme.text, 0.08));
		drawRoundedRect(item->hDC, item->rcItem, fill, border, 8);

		wchar_t text[128]{};
		GetWindowTextW(item->hwndItem, text, static_cast< int >(sizeof(text) / sizeof(text[0])));
		SetBkMode(item->hDC, TRANSPARENT);
		SetTextColor(item->hDC, disabled ? m_theme.overlay0 : (primary ? m_theme.onAccent : m_theme.text));
		HFONT oldFont = reinterpret_cast< HFONT >(SelectObject(item->hDC, m_uiFont));
		RECT textRect = item->rcItem;
		DrawTextW(item->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		SelectObject(item->hDC, oldFont);
		return true;
	}

	void createControls() {
		m_backgroundBrush = CreateSolidBrush(m_theme.mantle);
		m_panelBrush      = CreateSolidBrush(m_theme.base);
		m_logBrush        = CreateSolidBrush(m_theme.crust);

		m_uiFont    = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
		m_titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
		m_logFont   = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
		if (!m_logFont) {
			m_logFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
									CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
		}

		m_title  = CreateWindowExW(0, L"STATIC", L"Installing Mumble update", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd,
								   reinterpret_cast< HMENU >(ControlTitle), nullptr, nullptr);
		m_status = CreateWindowExW(0, L"STATIC", m_statusText.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd,
								   reinterpret_cast< HMENU >(ControlStatus), nullptr, nullptr);
		m_detailsButton =
			CreateWindowExW(0, L"BUTTON", L"Show details", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0,
							0, m_hwnd, reinterpret_cast< HMENU >(ControlDetails), nullptr, nullptr);
		m_closeButton =
			CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_OWNERDRAW, 0,
							0, 0, 0, m_hwnd, reinterpret_cast< HMENU >(ControlClose), nullptr, nullptr);
		m_log = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL, 0, 0, 0, 0,
								m_hwnd, reinterpret_cast< HMENU >(ControlLog), nullptr, nullptr);

		for (HWND control : { m_title, m_status, m_detailsButton, m_closeButton, m_log }) {
			SendMessageW(control, WM_SETFONT,
						 reinterpret_cast< WPARAM >(control == m_title ? m_titleFont
																	   : (control == m_log ? m_logFont : m_uiFont)),
						 TRUE);
		}
		SendMessageW(m_log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));

		layoutControls();
	}

	void layoutControls() {
		RECT client{};
		GetClientRect(m_hwnd, &client);
		const int width        = client.right - client.left;
		const int margin       = 22;
		const int buttonWidth  = 118;
		const int buttonHeight = 34;
		const int badgeSize    = 32;
		const int contentLeft  = margin + badgeSize + 12;

		m_badgeRect = { margin, 22, margin + badgeSize, 22 + badgeSize };
		MoveWindow(m_title, contentLeft, 22, width - contentLeft - margin, 26, TRUE);
		MoveWindow(m_status, contentLeft, 55, width - contentLeft - margin, 40, TRUE);
		m_progressRect = { margin, 108, width - margin, 122 };

		const int buttonTop = m_detailsVisible ? expandedHeight() - 56 : collapsedHeight() - 56;
		MoveWindow(m_detailsButton, margin, buttonTop, buttonWidth, buttonHeight, TRUE);
		MoveWindow(m_closeButton, width - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);

		if (m_detailsVisible) {
			MoveWindow(m_log, margin, 142, width - margin * 2, expandedHeight() - 214, TRUE);
			ShowWindow(m_log, SW_SHOWNORMAL);
		} else {
			ShowWindow(m_log, SW_HIDE);
		}
	}

	void resizeForDetails() {
		RECT window{};
		GetWindowRect(m_hwnd, &window);
		const DWORD style = static_cast< DWORD >(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
		RECT desired{ 0, 0, collapsedWidth(), m_detailsVisible ? expandedHeight() : collapsedHeight() };
		AdjustWindowRectEx(&desired, style, FALSE, 0);
		SetWindowPos(m_hwnd, nullptr, window.left, window.top, desired.right - desired.left,
					 desired.bottom - desired.top, SWP_NOZORDER | SWP_NOACTIVATE);
		layoutControls();
	}

	void toggleDetails() {
		m_detailsVisible = !m_detailsVisible;
		SetWindowTextW(m_detailsButton, m_detailsVisible ? L"Hide details" : L"Show details");
		resizeForDetails();
		refreshLogs(true);
	}

	void setStatus(const std::wstring &status) {
		m_statusText = status;
		SetWindowTextW(m_status, m_statusText.c_str());
	}

	void setProgress(const int percent, const bool indeterminate) {
		m_indeterminate = indeterminate;
		if (!indeterminate) {
			m_progressPercent = std::clamp(percent, 0, 100);
		}
		InvalidateRect(m_hwnd, &m_progressRect, FALSE);
	}

	void advanceSpinner() {
		if (m_completed) {
			return;
		}

		m_activityFrame = (m_activityFrame + 1) % 90;
		InvalidateRect(m_hwnd, &m_badgeRect, FALSE);
		if (m_indeterminate) {
			InvalidateRect(m_hwnd, &m_progressRect, FALSE);
		}
	}

	void appendLogSection(std::wstring &target, const std::wstring &label, const std::wstring &path) {
		const std::wstring text = readFileTail(path);
		if (text.empty()) {
			return;
		}

		if (!target.empty()) {
			target.append(L"\r\n");
		}
		target.append(label);
		target.append(L" - ");
		target.append(fileNameForDisplay(path));
		target.append(L"\r\n");
		target.append(text);
	}

	void refreshLogs(const bool force = false) {
		std::wstring updaterLog = readFileTail(m_options.updaterLogPath);
		if (!m_completed) {
			const std::wstring latestStatus = lastLogMessage(updaterLog);
			if (!latestStatus.empty()) {
				setStatus(latestStatus);
			}

			int parsedPercent = 0;
			if (parsePackageProgress(updaterLog, parsedPercent)) {
				setProgress(parsedPercent, false);
			}
		}

		std::wstring combined;
		if (!updaterLog.empty()) {
			combined.append(L"Updater log - ");
			combined.append(fileNameForDisplay(m_options.updaterLogPath));
			combined.append(L"\r\n");
			combined.append(updaterLog);
		}
		appendLogSection(combined, L"Windows Installer log", m_options.msiLogPath);
		if (combined.empty()) {
			combined = L"No installer log output yet.";
		}

		if (force || combined != m_lastLogText) {
			m_lastLogText = combined;
			SetWindowTextW(m_log, m_lastLogText.c_str());
			SendMessageW(m_log, EM_SETSEL, static_cast< WPARAM >(-1), static_cast< LPARAM >(-1));
			SendMessageW(m_log, EM_SCROLLCARET, 0, 0);
		}
	}

	void finish(const DWORD exitCode) {
		m_completed = true;
		m_exitCode  = static_cast< int >(exitCode);
		refreshLogs(true);
		EnableWindow(m_closeButton, TRUE);

		if (updateSucceeded(exitCode)) {
			setProgress(100, false);
			setStatus(m_options.noRelaunch ? L"Update completed." : L"Update completed. Restarting Mumble...");
			SetTimer(m_hwnd, UiAutoCloseTimer, UiAutoCloseDelayMsec, nullptr);
		} else if (updateCancelled(exitCode)) {
			setProgress(100, false);
			setStatus(L"Update cancelled. No fallback installer was run.");
		} else {
			setProgress(100, false);
			setStatus(L"Update failed with exit code " + std::to_wstring(exitCode) + L". Open details for logs.");
			if (!m_detailsVisible) {
				toggleDetails();
			}
		}
		InvalidateRect(m_hwnd, &m_badgeRect, FALSE);
	}
};

void postUiStatus(const std::wstring &message) {
	HWND hwnd = g_updaterWindow.load();
	if (!hwnd) {
		return;
	}

	auto text = std::make_unique< std::wstring >(message);
	if (PostMessageW(hwnd, UiStatusMessage, 0, reinterpret_cast< LPARAM >(text.get()))) {
		text.release();
	}
}

void postUiProgress(const int percent, const bool indeterminate) {
	HWND hwnd = g_updaterWindow.load();
	if (!hwnd) {
		return;
	}

	auto payload           = std::make_unique< UiProgressPayload >();
	payload->percent       = percent;
	payload->indeterminate = indeterminate;
	if (PostMessageW(hwnd, UiProgressMessage, 0, reinterpret_cast< LPARAM >(payload.get()))) {
		payload.release();
	}
}

bool rollbackPendingWindowsInstaller(const Options &options,
									 Mumble::UpdateHealth::PendingUpdate pending) {
	const std::filesystem::path updateRoot = packageWorkRoot(options);
	std::string healthError;
	const bool candidateRestartWasPending = pending.restartRequired;
	const std::string activeBootSession    = currentBootSessionIdentity();
	const bool crossedRebootBoundary = candidateRestartWasPending && !activeBootSession.empty()
								   && activeBootSession != pending.bootSessionIdentity;
	if (pending.mode != Mumble::UpdateHealth::TransactionMode::WindowsInstaller
		|| !pendingOwnsExpectedBackupRoot(updateRoot, pending)) {
		appendLog(options, L"Refusing Windows Installer rollback outside its transaction-owned snapshot.");
		return false;
	}
	bool recoveryExists              = false;
	const std::uint64_t recoverySize = fileSizeOrMissing(pending.recoveryInstallerPath, recoveryExists);
	if (!recoveryExists || recoverySize != pending.recoveryInstallerSize
		|| fileSha256(pending.recoveryInstallerPath) != pending.recoveryInstallerSha256) {
		appendLog(options, L"Refusing Windows Installer rollback because the known-good MSI failed verification.");
		return false;
	}

	Options recoveryOptions              = options;
	recoveryOptions.installerPath        = pending.recoveryInstallerPath.wstring();
	recoveryOptions.installerSha256      = wideFromUtf8(pending.recoveryInstallerSha256);
	recoveryOptions.recoveryInstallerPath.clear();
	recoveryOptions.recoveryInstallerSha256.clear();
	recoveryOptions.candidateExecutableSha256.clear();
	recoveryOptions.parentPid = 0;
	recoveryOptions.passive   = true;
	if (!recoveryOptions.msiLogPath.empty()) {
		recoveryOptions.msiLogPath =
			std::filesystem::path(recoveryOptions.msiLogPath).replace_filename(L"mumble-recovery-msi.log").wstring();
	}
	appendLog(options, L"Reinstalling the verified known-good MSI.");
	const DWORD rollbackExitCode = runInstaller(recoveryOptions);
	if (!updateSucceeded(rollbackExitCode)) {
		appendLog(options, L"Known-good MSI reinstall failed with code " + std::to_wstring(rollbackExitCode) + L'.');
		return false;
	}
	if (rollbackExitCode == RestartRequired
		|| (candidateRestartWasPending && !crossedRebootBoundary)) {
		// Never claim that recovery completed while Windows Installer still has
		// deferred file operations. This also applies when the candidate returned
		// 3010 but the recovery MSI returned 0 in the same boot: the candidate's
		// deferred operations can still win at reboot. Only a persistent startup
		// recovery invocation, after crossing that reboot boundary, may commit the
		// successful rollback and clear the durable journal.
		pending.state           = Mumble::UpdateHealth::TransactionState::RollbackArmed;
		pending.restartRequired = true;
		if (rollbackExitCode == RestartRequired && !activeBootSession.empty()) {
			// Recovery itself scheduled deferred operations. Bind the journal to
			// this boot so another genuine reboot is required before commit.
			pending.bootSessionIdentity = activeBootSession;
		}
		if (!Mumble::UpdateHealth::writePendingState(updateRoot, pending, &healthError)) {
			appendLog(options, L"Unable to persist reboot-required MSI recovery. " + wideFromUtf8(healthError));
		}
		appendLog(options, candidateRestartWasPending && rollbackExitCode != RestartRequired
							   ? L"Known-good MSI was restored, but the candidate's reboot boundary is still pending; "
								 L"recovery remains armed and uncommitted."
							   : L"Known-good MSI recovery requires reboot; recovery remains armed and uncommitted.");
		return false;
	}
	pending.restartRequired = false;
	if (!persistTerminalTransaction(options, updateRoot, pending,
									Mumble::UpdateHealth::TransactionState::RolledBack, healthError)) {
		appendLog(options, L"Unable to finalize Windows Installer rollback. " + wideFromUtf8(healthError));
		return false;
	}
	appendLog(options, L"Verified known-good MSI was restored.");
	return true;
}

bool rollbackPendingUpdate(const Options &options) {
	std::string healthError;
	auto pending = Mumble::UpdateHealth::readPendingState(packageWorkRoot(options),
															 std::filesystem::path(options.appPath), &healthError);
	if (!pending) {
		appendLog(options, L"No valid pending update rollback was available. " + wideFromUtf8(healthError));
		return false;
	}
	if (pending->mode == Mumble::UpdateHealth::TransactionMode::WindowsInstaller) {
		return rollbackPendingWindowsInstaller(options, *pending);
	}
	return rollbackPendingPackage(options);
}

std::optional< Mumble::UpdateHealth::PendingUpdate > armWindowsInstallerHealth(const Options &options) {
	if (options.recoveryInstallerPath.empty() || options.recoveryInstallerSha256.empty()
		|| options.candidateExecutableSha256.empty()) {
		return std::nullopt;
	}
	const std::filesystem::path updateRoot = packageWorkRoot(options);
	const std::filesystem::path appPath(options.appPath);
	std::string healthError;
	const std::filesystem::path statePath = Mumble::UpdateHealth::pendingStatePath(updateRoot, appPath);
	std::error_code stateError;
	const bool stateExists = std::filesystem::exists(statePath, stateError);
	auto previous = Mumble::UpdateHealth::readPendingState(updateRoot, appPath, &healthError);
	if (!previous && (stateExists || stateError)) {
		appendLog(options, L"Refusing MSI update because its existing recovery journal is unreadable. "
						   + wideFromUtf8(healthError));
		return std::nullopt;
	}
	if (previous) {
		if (previous->state == Mumble::UpdateHealth::TransactionState::Committed
			|| previous->state == Mumble::UpdateHealth::TransactionState::RolledBack) {
			if (!persistTerminalTransaction(options, updateRoot, *previous, previous->state, healthError)) {
				return std::nullopt;
			}
		} else if (Mumble::UpdateHealth::markerConfirmsHealthy(updateRoot, *previous, &healthError)) {
			if (!persistTerminalTransaction(options, updateRoot, *previous,
											Mumble::UpdateHealth::TransactionState::Committed, healthError)) {
				return std::nullopt;
			}
		} else if (!rollbackPendingUpdate(options)) {
			return std::nullopt;
		}
	}

	Mumble::UpdateHealth::PendingUpdate pending;
	pending.mode                    = Mumble::UpdateHealth::TransactionMode::WindowsInstaller;
	pending.transactionId           = newTransactionId();
	pending.state                   = Mumble::UpdateHealth::TransactionState::RollbackArmed;
	pending.packageIdentity         = lowerSha256(options.installerSha256);
	pending.previousPackageIdentity = lowerSha256(options.recoveryInstallerSha256);
	pending.expectedExecutableSha256 = lowerSha256(options.candidateExecutableSha256);
	pending.appPath                 = appPath;
	pending.bootSessionIdentity     = currentBootSessionIdentity();
	if (pending.bootSessionIdentity.empty()) {
		appendLog(options, L"Unable to obtain the Windows boot-session identity; refusing MSI protocol-v4 admission.");
		return std::nullopt;
	}
	try {
		pending.backupRoot = resolvePackagePathUnderRoot(
			updateRoot, "known-good/" + Mumble::UpdateHealth::installationKey(appPath.parent_path())
						+ "/transaction-" + pending.transactionId);
		pending.recoveryInstallerPath =
			resolvePackagePathUnderRoot(pending.backupRoot, "known-good-recovery.msi");
	} catch (const std::exception &exception) {
		appendLog(options, L"Unable to create MSI recovery snapshot path. " + wideFromUtf8(exception.what()));
		return std::nullopt;
	}

	std::error_code filesystemError;
	std::filesystem::create_directories(pending.backupRoot, filesystemError);
	if (filesystemError || !pendingOwnsExpectedBackupRoot(updateRoot, pending)
		|| !ensureDurableRecoveryFile(std::filesystem::path(options.recoveryInstallerPath),
									 pending.recoveryInstallerPath)) {
		appendLog(options, L"Unable to persist the known-good MSI before candidate installation.");
		std::filesystem::remove_all(pending.backupRoot, filesystemError);
		return std::nullopt;
	}
	bool recoveryExists              = false;
	pending.recoveryInstallerSize    = fileSizeOrMissing(pending.recoveryInstallerPath, recoveryExists);
	pending.recoveryInstallerSha256  = fileSha256(pending.recoveryInstallerPath);
	if (!recoveryExists || pending.recoveryInstallerSize == 0
		|| pending.recoveryInstallerSha256 != lowerSha256(options.recoveryInstallerSha256)) {
		appendLog(options, L"Persisted known-good MSI failed its mandatory SHA256 check.");
		std::filesystem::remove_all(pending.backupRoot, filesystemError);
		return std::nullopt;
	}
	if (!Mumble::UpdateHealth::writePendingState(updateRoot, pending, &healthError)) {
		appendLog(options, L"Unable to arm MSI update-health state. " + wideFromUtf8(healthError));
		std::filesystem::remove_all(pending.backupRoot, filesystemError);
		return std::nullopt;
	}
	if (!armRecoveryBootstrap(options) || !launchRecoveryWatchdog(options)) {
		appendLog(options, L"Unable to start persistent MSI recovery before installation.");
		Mumble::UpdateHealth::removePendingState(updateRoot, appPath, &healthError);
		clearRecoveryRunOnce(options);
		std::filesystem::remove_all(pending.backupRoot, filesystemError);
		return std::nullopt;
	}
	appendLog(options, L"Verified the known-good MSI and durably armed updater protocol v4 recovery.");
	return pending;
}

bool installerFallbackHasNoPendingNativeTransaction(const Options &options) {
	const std::filesystem::path statePath = Mumble::UpdateHealth::pendingStatePath(
		packageWorkRoot(options), std::filesystem::path(options.appPath));
	std::error_code error;
	const bool exists = std::filesystem::exists(statePath, error);
	return !error && !exists;
}

DWORD runUpdate(const Options &requestedOptions) {
	Options options = requestedOptions;
	appendLog(options, L"MumbleUpdater started.");
	postUiProgress(-1, true);
	const bool packageMode = !options.packagePath.empty();
	std::unique_ptr< InstallationMutex > installationMutex;
	std::unique_ptr< VerifiedArtifactFile > verifiedPackage;
	std::unique_ptr< VerifiedArtifactFile > verifiedInstaller;
	std::unique_ptr< VerifiedArtifactFile > verifiedRecoveryInstaller;
	if (options.recoverOnly || packageMode || !options.installerPath.empty()) {
		try {
			installationMutex = std::make_unique< InstallationMutex >(std::filesystem::path(options.appPath));
			options.appPath = installationMutex->resolvedAppPath(std::filesystem::path(options.appPath)).wstring();
			options.workingDirectory = parentPath(options.appPath).wstring();
			if (!options.updaterLogPath.empty()) {
				std::error_code updateRootError;
				const std::filesystem::path physicalUpdateRoot =
					std::filesystem::canonical(packageWorkRoot(options), updateRootError);
				if (updateRootError || physicalUpdateRoot.empty()) {
					throw std::runtime_error("Unable to resolve the physical updater work directory.");
				}
				options.updaterLogPath =
					(physicalUpdateRoot / std::filesystem::path(options.updaterLogPath).filename()).wstring();
				if (!options.msiLogPath.empty()) {
					options.msiLogPath =
						(physicalUpdateRoot / std::filesystem::path(options.msiLogPath).filename()).wstring();
				}
			}
			if (packageMode) {
				verifiedPackage = std::make_unique< VerifiedArtifactFile >(
					std::filesystem::path(options.packagePath), lowerSha256(options.packageSha256), "update package");
				options.packagePath = verifiedPackage->finalPath().wstring();
			}
			if (!options.installerPath.empty()) {
				verifiedInstaller = std::make_unique< VerifiedArtifactFile >(
					std::filesystem::path(options.installerPath), lowerSha256(options.installerSha256), "MSI installer");
				options.installerPath = verifiedInstaller->finalPath().wstring();
			}
			if (!options.recoveryInstallerPath.empty()) {
				verifiedRecoveryInstaller = std::make_unique< VerifiedArtifactFile >(
					std::filesystem::path(options.recoveryInstallerPath),
					lowerSha256(options.recoveryInstallerSha256), "known-good MSI installer");
				options.recoveryInstallerPath = verifiedRecoveryInstaller->finalPath().wstring();
			}
		} catch (const std::exception &exception) {
			appendLog(options, L"Updater transaction admission failed. " + wideFromUtf8(exception.what()));
			return ERROR_LOCK_FAILED;
		}
	}
	if (options.recoverOnly) {
		const DWORD recoveryExitCode = runPendingRecovery(options);
		appendLog(options, L"MumbleUpdater recovery finished.");
		return recoveryExitCode;
	}

	if (packageMode && options.prepareOnly) {
		appendLog(options, L"Preparing update package.");
		const DWORD prepareExitCode = runPackageUpdate(options);
		appendLog(options, L"MumbleUpdater finished.");
		return prepareExitCode;
	}

	waitForParent(options);

	appendLog(options, packageMode ? L"Applying update package." : L"Running Windows Installer.");
	bool installerManagedClientRelaunch = false;
	DWORD updateExitCode = packageMode ? runPackageUpdate(options)
									  : runHealthQualifiedInstaller(options, &installerManagedClientRelaunch);
	bool installerRan    = !packageMode;
	bool packageRestartAttempted = false;

	if (packageMode && !updateSucceeded(updateExitCode) && !updateCancelled(updateExitCode)
		&& fileExists(options.installerPath) && installerFallbackHasNoPendingNativeTransaction(options)) {
		appendLog(options, L"Package update failed with code " + std::to_wstring(updateExitCode)
							   + L"; running verified MSI fallback.");
		postUiProgress(-1, true);
		updateExitCode = runHealthQualifiedInstaller(options, &installerManagedClientRelaunch);
		installerRan   = true;
	} else if (packageMode && updateCancelled(updateExitCode)) {
		appendLog(options, L"Package update was cancelled; MSI fallback will not run.");
	}
	if (packageMode && !installerRan && options.noRelaunch && updateSucceeded(updateExitCode)) {
		const std::filesystem::path statePath = Mumble::UpdateHealth::pendingStatePath(
			packageWorkRoot(options), std::filesystem::path(options.appPath));
		std::error_code stateError;
		const bool stateExists = std::filesystem::exists(statePath, stateError);
		std::string healthError;
		auto pending = Mumble::UpdateHealth::readPendingState(packageWorkRoot(options),
													  std::filesystem::path(options.appPath), &healthError);
		if (stateError || (stateExists && !pending)) {
			appendLog(options, L"No-relaunch update left an unreadable health journal; refusing success.");
			updateExitCode = ERROR_RECOVERY_FAILURE;
		} else if (pending && pending->state == Mumble::UpdateHealth::TransactionState::AwaitingHealth) {
			appendLog(options, L"A health-required package cannot succeed with --no-relaunch; rolling it back now.");
			updateExitCode = rollbackPendingPackage(options) ? ERROR_PROCESS_ABORTED : ERROR_RECOVERY_FAILURE;
		}
	}

	if (installerRan && options.recoveryInstallerPath.empty() && !options.noRelaunch && updateSucceeded(updateExitCode)) {
		appendLog(options, L"Windows Installer completed; preparing to restart Mumble.");
		postUiProgress(100, false);
		Sleep(800);
		RelaunchResult restart = relaunchMumble(options);
		if (restart.process) {
			CloseHandle(restart.process);
		}
		if (!restart.launched) {
			updateExitCode = restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error;
		}
	} else if (packageMode && !installerRan && !options.noRelaunch && updateSucceeded(updateExitCode)) {
		appendLog(options, L"Package update completed; preparing to restart Mumble.");
		postUiProgress(100, false);
		Sleep(800);
		packageRestartAttempted = true;
		updateExitCode = restartPackageAndQualify(options);
	} else if (packageMode && !installerRan && updateCancelled(updateExitCode) && !options.noRelaunch) {
		appendLog(options, L"Update was cancelled; restarting Mumble without applying the MSI fallback.");
		postUiProgress(100, false);
		Sleep(800);
		RelaunchResult restart = relaunchMumble(options);
		if (restart.process) {
			CloseHandle(restart.process);
		}
		if (!restart.launched) {
			updateExitCode = restart.error == 0 ? ERROR_PROCESS_ABORTED : restart.error;
		}
	} else if (installerRan && !options.noRelaunch && !updateSucceeded(updateExitCode)
			   && !installerManagedClientRelaunch) {
		const std::filesystem::path statePath = Mumble::UpdateHealth::pendingStatePath(
			packageWorkRoot(options), std::filesystem::path(options.appPath));
		std::error_code stateError;
		const bool stateExists = std::filesystem::exists(statePath, stateError);
		if (!stateError && !stateExists && fileExists(options.appPath)) {
			appendLog(options,
					  L"Windows Installer did not complete; restarting the unchanged or restored Mumble client.");
			postUiProgress(100, false);
			Sleep(800);
			RelaunchResult restart = relaunchMumble(options);
			if (restart.process) {
				CloseHandle(restart.process);
			}
			if (!restart.launched) {
				appendLog(options, L"The existing Mumble client could not be restarted automatically.");
			}
		} else {
			appendLog(options,
					  L"Windows Installer did not complete and recovery is still pending; Mumble will not be "
					  L"restarted into an uncertain installation.");
		}
	}

	if (packageMode && !installerRan && !updateSucceeded(updateExitCode) && !updateCancelled(updateExitCode)) {
		if (installerFallbackHasNoPendingNativeTransaction(options)) {
			std::string fallbackError;
			if (Mumble::UpdateHealth::writeInstallerFallbackRequest(
					packageWorkRoot(options), std::filesystem::path(options.appPath), lowerSha256(options.packageSha256),
					static_cast< std::uint32_t >(updateExitCode), &fallbackError)) {
				appendLog(options,
						  L"Package update failed safely; the restored client will use the verified MSI on its next "
						  L"update attempt.");
				if (!options.noRelaunch && !packageRestartAttempted) {
					appendLog(options, L"Restarting the restored client so it can offer the verified MSI fallback.");
					postUiProgress(100, false);
					Sleep(800);
					RelaunchResult restart = relaunchMumble(options);
					if (restart.process) {
						CloseHandle(restart.process);
					}
					if (!restart.launched) {
						appendLog(options, L"The restored client could not be restarted automatically.");
					}
				}
			} else {
				appendLog(options, L"Package update failed safely, but the MSI fallback request could not be persisted. "
									 + wideFromUtf8(fallbackError));
			}
		} else {
			appendLog(options,
					  L"Package update failed and no safe verified MSI fallback was available; the native recovery "
					  L"journal must be resolved first.");
		}
	}

	if (updateSucceeded(updateExitCode)) {
		std::string fallbackCleanupError;
		if (!Mumble::UpdateHealth::removeInstallerFallbackRequest(
				packageWorkRoot(options), std::filesystem::path(options.appPath), &fallbackCleanupError)) {
			appendLog(options, L"Unable to remove a stale MSI fallback request. "
								   + wideFromUtf8(fallbackCleanupError));
		}
	}

	appendLog(options, L"MumbleUpdater finished.");
	return updateExitCode;
}

DWORD runUpdateSafely(const Options &options) noexcept {
	try {
		return runUpdate(options);
	} catch (const std::exception &exception) {
		appendLog(options, L"Unhandled updater failure was contained. " + wideFromUtf8(exception.what()));
		return ERROR_UNHANDLED_EXCEPTION;
	} catch (...) {
		appendLog(options, L"Unhandled non-standard updater failure was contained.");
		return ERROR_UNHANDLED_EXCEPTION;
	}
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
	int argc       = 0;
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

	if (options.noUi) {
		return static_cast< int >(runUpdateSafely(options));
	}

	UpdaterProgressWindow window(options);
	if (!window.create(instance)) {
		return static_cast< int >(runUpdateSafely(options));
	}

	std::thread worker([options]() {
		const DWORD exitCode = runUpdateSafely(options);
		HWND hwnd            = g_updaterWindow.load();
		if (hwnd) {
			PostMessageW(hwnd, UiDoneMessage, exitCode, 0);
		}
	});

	const int exitCode = window.run();
	if (worker.joinable()) {
		worker.join();
	}
	return exitCode;
}
