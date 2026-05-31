// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr DWORD ParentExitTimeoutMsec = 180000;
constexpr DWORD RestartRequired       = 3010;

struct Options {
	DWORD parentPid = 0;
	std::wstring installerPath;
	std::wstring packagePath;
	std::wstring appPath;
	std::wstring workingDirectory;
	std::wstring updaterLogPath;
	std::wstring msiLogPath;
	bool passive = true;
	bool noRelaunch = false;
	bool elevatedRetry = false;
	bool noUi = false;
};

void postUiStatus(const std::wstring &message);
void postUiProgress(int percent, bool indeterminate);

std::string utf8FromWide(const std::wstring &value) {
	if (value.empty()) {
		return {};
	}

	const int byteCount = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast< int >(value.size()), nullptr, 0,
											  nullptr, nullptr);
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

void appendLog(const Options &options, const std::wstring &message) {
	if (options.updaterLogPath.empty()) {
		return;
	}

	std::error_code error;
	const std::filesystem::path logPath(options.updaterLogPath);
	if (logPath.has_parent_path()) {
		std::filesystem::create_directories(logPath.parent_path(), error);
	}

	std::ofstream stream(logPath, std::ios::app | std::ios::binary);
	if (!stream) {
		return;
	}

	SYSTEMTIME now;
	GetLocalTime(&now);
	std::wostringstream line;
	line << L'[' << std::setfill(L'0') << std::setw(4) << now.wYear << L'-' << std::setw(2) << now.wMonth << L'-'
		 << std::setw(2) << now.wDay << L' ' << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute
		 << L':' << std::setw(2) << now.wSecond << L"] " << message;
	stream << utf8FromWide(line.str()) << '\n';
	postUiStatus(message);
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
		} else if (arg == L"--passive") {
			options.passive = true;
		} else if (arg == L"--no-passive") {
			options.passive = false;
		} else if (arg == L"--no-relaunch") {
			options.noRelaunch = true;
		} else if (arg == L"--elevated-retry") {
			options.elevatedRetry = true;
		} else if (arg == L"--no-ui") {
			options.noUi = true;
		}
	}

	const bool hasInstaller = fileExists(options.installerPath);
	const bool hasPackage   = fileExists(options.packagePath);
	return fileExists(options.appPath) && (hasInstaller != hasPackage);
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

std::wstring packageUpdaterArguments(const Options &options, const bool elevatedRetry, const bool noUi = false) {
	std::wstring arguments;
	if (options.parentPid != 0) {
		arguments += L"--parent-pid " + std::to_wstring(options.parentPid) + L' ';
	}
	arguments += L"--package " + quoteArgument(options.packagePath);
	arguments += L" --app " + quoteArgument(options.appPath);
	if (!options.workingDirectory.empty()) {
		arguments += L" --working-dir " + quoteArgument(options.workingDirectory);
	}
	if (!options.updaterLogPath.empty()) {
		arguments += L" --updater-log " + quoteArgument(options.updaterLogPath);
	}
	if (!options.msiLogPath.empty()) {
		arguments += L" --msi-log " + quoteArgument(options.msiLogPath);
	}
	if (!options.passive) {
		arguments += L" --no-passive";
	}
	if (options.noRelaunch) {
		arguments += L" --no-relaunch";
	}
	if (elevatedRetry) {
		arguments += L" --elevated-retry";
	}
	if (noUi) {
		arguments += L" --no-ui";
	}
	return arguments;
}

DWORD relaunchElevatedForPackage(const Options &options) {
	const std::wstring executable = currentExecutablePath();
	const std::wstring parameters = packageUpdaterArguments(options, true, true);

	SHELLEXECUTEINFOW executeInfo {};
	executeInfo.cbSize       = sizeof(executeInfo);
	executeInfo.fMask        = SEE_MASK_NOCLOSEPROCESS;
	executeInfo.lpVerb       = L"runas";
	executeInfo.lpFile       = executable.c_str();
	executeInfo.lpParameters = parameters.c_str();
	executeInfo.lpDirectory  = options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str();
	executeInfo.nShow        = SW_SHOWNORMAL;

	appendLog(options, L"App directory is not writable; relaunching updater elevated.");
	if (!ShellExecuteExW(&executeInfo)) {
		const DWORD error = GetLastError();
		appendLog(options, L"Failed to relaunch updater elevated. Error " + std::to_wstring(error) + L'.');
		return error == 0 ? 1 : error;
	}

	if (executeInfo.hProcess) {
		WaitForSingleObject(executeInfo.hProcess, INFINITE);
		DWORD exitCode = 0;
		if (!GetExitCodeProcess(executeInfo.hProcess, &exitCode)) {
			exitCode = GetLastError();
		}
		CloseHandle(executeInfo.hProcess);
		return exitCode;
	}

	return 0;
}

bool writePackageApplyScript(const Options &options, const std::filesystem::path &scriptPath) {
	std::error_code error;
	std::filesystem::create_directories(scriptPath.parent_path(), error);
	if (error) {
		appendLog(options, L"Unable to create package script directory. Error " + std::to_wstring(error.value()) + L'.');
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
	Add-Content -LiteralPath $UpdaterLogPath -Encoding UTF8 -Value ("[{0}] {1}" -f (Get-Date).ToString('s'), $Message)
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
	if ($minUpdaterVersion -gt 2) {
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

	SHELLEXECUTEINFOW executeInfo {};
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

DWORD runPackageUpdate(const Options &options) {
	const std::filesystem::path appDir = parentPath(options.appPath);
	if (appDir.empty()) {
		appendLog(options, L"Unable to resolve Mumble app directory.");
		return 2;
	}

	if (!directoryWritable(options, appDir)) {
		if (options.elevatedRetry) {
			appendLog(options, L"App directory is still not writable after elevation retry.");
			return ERROR_ACCESS_DENIED;
		}
		return relaunchElevatedForPackage(options);
	}

	const std::filesystem::path workRoot = packageWorkRoot(options);
	const std::filesystem::path scriptPath =
		workRoot / (L"mumble-apply-update-" + std::to_wstring(GetCurrentProcessId()) + L".ps1");
	if (!writePackageApplyScript(options, scriptPath)) {
		return 2;
	}

	const DWORD exitCode = runPowerShellPackageApply(options, scriptPath);
	std::error_code error;
	std::filesystem::remove(scriptPath, error);
	return exitCode;
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

constexpr UINT UiStatusMessage   = WM_APP + 1;
constexpr UINT UiProgressMessage = WM_APP + 2;
constexpr UINT UiDoneMessage     = WM_APP + 3;
constexpr UINT_PTR UiRefreshTimer = 1;
constexpr UINT_PTR UiAutoCloseTimer = 2;
constexpr DWORD UiRefreshIntervalMsec = 350;
constexpr DWORD UiAutoCloseDelayMsec = 1800;
constexpr std::uintmax_t MaxLogTailBytes = 256 * 1024;

enum : int {
	ControlSpinner = 1001,
	ControlTitle,
	ControlStatus,
	ControlProgress,
	ControlDetails,
	ControlClose,
	ControlLog
};

struct UiProgressPayload {
	int percent = 0;
	bool indeterminate = true;
};

std::atomic< HWND > g_updaterWindow { nullptr };

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

	const unsigned char *raw = reinterpret_cast< const unsigned char * >(bytes.data());
	const bool hasUtf16LeBom = bytes.size() >= 2 && raw[0] == 0xff && raw[1] == 0xfe;
	const std::size_t sampleSize = std::min< std::size_t >(bytes.size(), 4096);
	std::size_t evenNuls = 0;
	std::size_t oddNuls  = 0;
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
			const wchar_t ch = static_cast< wchar_t >(static_cast< unsigned char >(bytes[byteIndex])
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

	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
							  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
							  FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}

	LARGE_INTEGER size {};
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

	LARGE_INTEGER distance {};
	distance.QuadPart = static_cast< LONGLONG >(start);
	if (!SetFilePointerEx(file, distance, nullptr, FILE_BEGIN)) {
		CloseHandle(file);
		return {};
	}

	const DWORD bytesToRead = static_cast< DWORD >(
		std::min< std::uintmax_t >(MaxLogTailBytes, static_cast< std::uintmax_t >(size.QuadPart) - start));
	std::vector< char > buffer(bytesToRead);
	DWORD bytesRead = 0;
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

	std::size_t start = text.find_last_of(L"\r\n", end);
	start = start == std::wstring::npos ? 0 : start + 1;
	std::wstring line = text.substr(start, end - start + 1);
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
	wchar_t *end = nullptr;
	const long current = std::wcstol(cursor, &end, 10);
	if (!end || *end != L'/') {
		return false;
	}
	cursor = end + 1;
	const long total = std::wcstol(cursor, &end, 10);
	if (total <= 0 || current < 0) {
		return false;
	}

	percent = static_cast< int >(std::clamp((current * 100) / total, 0L, 100L));
	return true;
}

class UpdaterProgressWindow {
public:
	explicit UpdaterProgressWindow(const Options &options) : m_options(options) {}

	bool create(HINSTANCE instance) {
		INITCOMMONCONTROLSEX commonControls {};
		commonControls.dwSize = sizeof(commonControls);
		commonControls.dwICC  = ICC_PROGRESS_CLASS;
		InitCommonControlsEx(&commonControls);

		WNDCLASSW windowClass {};
		windowClass.lpfnWndProc   = &UpdaterProgressWindow::windowProc;
		windowClass.hInstance     = instance;
		windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
		windowClass.hbrBackground = reinterpret_cast< HBRUSH >(COLOR_WINDOW + 1);
		windowClass.lpszClassName = L"MumbleUpdaterProgressWindow";
		RegisterClassW(&windowClass);

		const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		RECT rect { 0, 0, collapsedWidth(), collapsedHeight() };
		AdjustWindowRectEx(&rect, style, FALSE, 0);

		m_hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"Mumble update", style, CW_USEDEFAULT, CW_USEDEFAULT,
								 rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
		if (!m_hwnd) {
			return false;
		}

		g_updaterWindow.store(m_hwnd);
		ShowWindow(m_hwnd, SW_SHOWNORMAL);
		UpdateWindow(m_hwnd);
		return true;
	}

	int run() {
		MSG message {};
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		g_updaterWindow.store(nullptr);
		return m_exitCode;
	}

private:
	Options m_options;
	HWND m_hwnd = nullptr;
	HWND m_spinner = nullptr;
	HWND m_title = nullptr;
	HWND m_status = nullptr;
	HWND m_progress = nullptr;
	HWND m_detailsButton = nullptr;
	HWND m_closeButton = nullptr;
	HWND m_log = nullptr;
	HFONT m_uiFont = nullptr;
	HFONT m_logFont = nullptr;
	bool m_detailsVisible = false;
	bool m_completed = false;
	bool m_indeterminate = true;
	int m_exitCode = 1;
	int m_spinnerFrame = 0;
	std::wstring m_lastLogText;
	std::wstring m_statusText = L"Preparing update...";

	int collapsedWidth() const { return 560; }
	int collapsedHeight() const { return 180; }
	int expandedHeight() const { return 500; }

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		UpdaterProgressWindow *self = nullptr;
		if (message == WM_NCCREATE) {
			auto *create = reinterpret_cast< CREATESTRUCTW * >(lParam);
			self = static_cast< UpdaterProgressWindow * >(create->lpCreateParams);
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
			case WM_SIZE:
				layoutControls();
				return 0;
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
				PostQuitMessage(0);
				return 0;
		}

		return DefWindowProcW(m_hwnd, message, wParam, lParam);
	}

	void createControls() {
		m_uiFont = reinterpret_cast< HFONT >(GetStockObject(DEFAULT_GUI_FONT));
		m_logFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

		m_spinner = CreateWindowExW(0, L"STATIC", L"|", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, m_hwnd,
									reinterpret_cast< HMENU >(ControlSpinner), nullptr, nullptr);
		m_title = CreateWindowExW(0, L"STATIC", L"Installing Mumble update", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
								  m_hwnd, reinterpret_cast< HMENU >(ControlTitle), nullptr, nullptr);
		m_status = CreateWindowExW(0, L"STATIC", m_statusText.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd,
									reinterpret_cast< HMENU >(ControlStatus), nullptr, nullptr);
		m_progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 0, 0, 0, 0,
									 m_hwnd, reinterpret_cast< HMENU >(ControlProgress), nullptr, nullptr);
		m_detailsButton = CreateWindowExW(0, L"BUTTON", L"Show details", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
										  0, 0, m_hwnd, reinterpret_cast< HMENU >(ControlDetails), nullptr, nullptr);
		m_closeButton = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED, 0,
										0, 0, 0, m_hwnd, reinterpret_cast< HMENU >(ControlClose), nullptr, nullptr);
		m_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
								WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL,
								0, 0, 0, 0, m_hwnd, reinterpret_cast< HMENU >(ControlLog), nullptr, nullptr);

		for (HWND control : { m_spinner, m_title, m_status, m_progress, m_detailsButton, m_closeButton, m_log }) {
			SendMessageW(control, WM_SETFONT, reinterpret_cast< WPARAM >(control == m_log ? m_logFont : m_uiFont),
						 TRUE);
		}

		layoutControls();
	}

	void layoutControls() {
		RECT client {};
		GetClientRect(m_hwnd, &client);
		const int width = client.right - client.left;
		const int margin = 18;
		const int buttonWidth = 104;
		const int buttonHeight = 28;
		const int spinnerSize = 28;
		const int contentLeft = margin + spinnerSize + 12;

		MoveWindow(m_spinner, margin, 22, spinnerSize, 24, TRUE);
		MoveWindow(m_title, contentLeft, 18, width - contentLeft - margin, 24, TRUE);
		MoveWindow(m_status, contentLeft, 48, width - contentLeft - margin, 42, TRUE);
		MoveWindow(m_progress, margin, 98, width - margin * 2, 18, TRUE);

		const int buttonTop = m_detailsVisible ? expandedHeight() - 48 : collapsedHeight() - 48;
		MoveWindow(m_detailsButton, margin, buttonTop, buttonWidth, buttonHeight, TRUE);
		MoveWindow(m_closeButton, width - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);

		if (m_detailsVisible) {
			MoveWindow(m_log, margin, 132, width - margin * 2, expandedHeight() - 190, TRUE);
			ShowWindow(m_log, SW_SHOWNORMAL);
		} else {
			ShowWindow(m_log, SW_HIDE);
		}
	}

	void resizeForDetails() {
		RECT window {};
		GetWindowRect(m_hwnd, &window);
		const DWORD style = static_cast< DWORD >(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
		RECT desired { 0, 0, collapsedWidth(), m_detailsVisible ? expandedHeight() : collapsedHeight() };
		AdjustWindowRectEx(&desired, style, FALSE, 0);
		SetWindowPos(m_hwnd, nullptr, window.left, window.top, desired.right - desired.left, desired.bottom - desired.top,
					 SWP_NOZORDER | SWP_NOACTIVATE);
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
		if (m_progress) {
			SendMessageW(m_progress, PBM_SETMARQUEE, indeterminate ? TRUE : FALSE, 30);
			if (!indeterminate) {
				SendMessageW(m_progress, PBM_SETRANGE32, 0, 100);
				SendMessageW(m_progress, PBM_SETPOS, std::clamp(percent, 0, 100), 0);
			}
		}
	}

	void advanceSpinner() {
		if (m_completed) {
			return;
		}

		static const wchar_t *frames[] = { L"|", L"/", L"-", L"\\" };
		m_spinnerFrame = (m_spinnerFrame + 1) % 4;
		SetWindowTextW(m_spinner, frames[m_spinnerFrame]);
		if (m_indeterminate && m_progress) {
			SendMessageW(m_progress, PBM_SETMARQUEE, TRUE, 30);
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
		target.append(L"===== ");
		target.append(label);
		target.append(L": ");
		target.append(fileNameForDisplay(path));
		target.append(L" =====\r\n");
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
			combined.append(L"===== Updater log: ");
			combined.append(fileNameForDisplay(m_options.updaterLogPath));
			combined.append(L" =====\r\n");
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
		m_exitCode = static_cast< int >(exitCode);
		refreshLogs(true);
		EnableWindow(m_closeButton, TRUE);
		SendMessageW(m_progress, PBM_SETMARQUEE, FALSE, 0);

		if (exitCode == 0 || exitCode == RestartRequired) {
			SetWindowTextW(m_spinner, L"OK");
			setProgress(100, false);
			setStatus(m_options.noRelaunch ? L"Update completed." : L"Update completed. Restarting Mumble...");
			SetTimer(m_hwnd, UiAutoCloseTimer, UiAutoCloseDelayMsec, nullptr);
		} else {
			SetWindowTextW(m_spinner, L"!");
			setProgress(100, false);
			setStatus(L"Update failed with exit code " + std::to_wstring(exitCode) + L". Open details for logs.");
			if (!m_detailsVisible) {
				toggleDetails();
			}
		}
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

	auto payload = std::make_unique< UiProgressPayload >();
	payload->percent = percent;
	payload->indeterminate = indeterminate;
	if (PostMessageW(hwnd, UiProgressMessage, 0, reinterpret_cast< LPARAM >(payload.get()))) {
		payload.release();
	}
}

DWORD runUpdate(const Options &options) {
	appendLog(options, L"MumbleUpdater started.");
	postUiProgress(-1, true);
	waitForParent(options);

	const bool packageMode = !options.packagePath.empty();
	appendLog(options, packageMode ? L"Applying update package." : L"Running Windows Installer.");
	const DWORD updateExitCode = packageMode ? runPackageUpdate(options) : runInstaller(options);
	if (!packageMode && !options.noRelaunch && (updateExitCode == 0 || updateExitCode == RestartRequired)) {
		appendLog(options, L"Windows Installer completed; preparing to restart Mumble.");
		postUiProgress(100, false);
		Sleep(800);
		relaunchMumble(options);
	}

	appendLog(options, L"MumbleUpdater finished.");
	return updateExitCode;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
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

	if (options.noUi) {
		return static_cast< int >(runUpdate(options));
	}

	UpdaterProgressWindow window(options);
	if (!window.create(instance)) {
		return static_cast< int >(runUpdate(options));
	}

	std::thread worker([options]() {
		const DWORD exitCode = runUpdate(options);
		HWND hwnd = g_updaterWindow.load();
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
