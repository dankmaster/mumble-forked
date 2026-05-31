// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
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
constexpr DWORD InstallerUserExit     = 1602;

bool updateSucceeded(const DWORD exitCode) {
	return exitCode == 0 || exitCode == RestartRequired;
}

bool updateCancelled(const DWORD exitCode) {
	return exitCode == ERROR_CANCELLED || exitCode == InstallerUserExit;
}

struct Options {
	DWORD parentPid = 0;
	std::wstring installerPath;
	std::wstring packagePath;
	std::wstring appPath;
	std::wstring workingDirectory;
	std::wstring updaterLogPath;
	std::wstring msiLogPath;
	std::wstring uiTheme;
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
		 << std::setw(2) << now.wDay << L' ' << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute
		 << L':' << std::setw(2) << now.wSecond << L"] " << message;

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
	if (!hasInstaller) {
		options.installerPath.clear();
	}
	if (!hasPackage) {
		options.packagePath.clear();
	}
	return fileExists(options.appPath) && (hasInstaller || hasPackage);
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
	if (!options.installerPath.empty()) {
		arguments += L" --installer " + quoteArgument(options.installerPath);
	}
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
	if (!options.uiTheme.empty()) {
		arguments += L" --ui-theme " + quoteArgument(options.uiTheme);
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
constexpr int MumbleUpdaterIconResourceId = 101;

enum : int {
	ControlTitle = 1001,
	ControlStatus,
	ControlDetails,
	ControlClose,
	ControlLog
};

struct UiProgressPayload {
	int percent = 0;
	bool indeterminate = true;
};

std::atomic< HWND > g_updaterWindow { nullptr };

struct UpdaterTheme {
	COLORREF crust = RGB(25, 31, 38);
	COLORREF mantle = RGB(37, 44, 52);
	COLORREF base = RGB(25, 31, 38);
	COLORREF surface0 = RGB(49, 58, 68);
	COLORREF surface1 = RGB(57, 66, 77);
	COLORREF surface2 = RGB(52, 61, 72);
	COLORREF text = RGB(224, 231, 239);
	COLORREF subtext0 = RGB(125, 137, 150);
	COLORREF overlay0 = RGB(125, 137, 150);
	COLORREF accent = RGB(106, 166, 207);
	COLORREF accentHover = RGB(130, 193, 224);
	COLORREF success = RGB(105, 178, 140);
	COLORREF warning = RGB(199, 146, 91);
	COLORREF danger = RGB(196, 106, 116);
	COLORREF onAccent = RGB(25, 31, 38);
	COLORREF caption = RGB(25, 31, 38);
	COLORREF captionText = RGB(224, 231, 239);
	COLORREF captionBorder = RGB(57, 66, 77);
	bool dark = true;
};

int colorRed(const COLORREF color) { return GetRValue(color); }
int colorGreen(const COLORREF color) { return GetGValue(color); }
int colorBlue(const COLORREF color) { return GetBValue(color); }

COLORREF mixColors(const COLORREF base, const COLORREF overlay, const double overlayRatio) {
	const double clampedRatio = std::clamp(overlayRatio, 0.0, 1.0);
	const double baseRatio = 1.0 - clampedRatio;
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
	color = RGB(static_cast< int >((raw >> 16) & 0xff), static_cast< int >((raw >> 8) & 0xff),
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
			const std::wstring key = lowerAscii(entry.substr(0, separator));
			const std::wstring value = entry.substr(separator + 1);
			COLORREF parsedColor = 0;
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
	explicit UpdaterProgressWindow(const Options &options) : m_options(options), m_theme(themeFromOptions(options)) {}

	bool create(HINSTANCE instance) {
		HICON largeIcon = reinterpret_cast< HICON >(LoadImageW(
			instance, MAKEINTRESOURCEW(MumbleUpdaterIconResourceId), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
			GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
		HICON smallIcon = reinterpret_cast< HICON >(LoadImageW(
			instance, MAKEINTRESOURCEW(MumbleUpdaterIconResourceId), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
			GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
		if (!largeIcon) {
			largeIcon = LoadIconW(nullptr, IDI_APPLICATION);
		}
		if (!smallIcon) {
			smallIcon = largeIcon;
		}

		WNDCLASSEXW windowClass {};
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
		RECT rect { 0, 0, collapsedWidth(), collapsedHeight() };
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
	UpdaterTheme m_theme;
	HWND m_hwnd = nullptr;
	HWND m_title = nullptr;
	HWND m_status = nullptr;
	HWND m_detailsButton = nullptr;
	HWND m_closeButton = nullptr;
	HWND m_log = nullptr;
	RECT m_badgeRect {};
	RECT m_progressRect {};
	HFONT m_uiFont = nullptr;
	HFONT m_titleFont = nullptr;
	HFONT m_logFont = nullptr;
	HBRUSH m_backgroundBrush = nullptr;
	HBRUSH m_panelBrush = nullptr;
	HBRUSH m_logBrush = nullptr;
	bool m_detailsVisible = false;
	bool m_completed = false;
	bool m_indeterminate = true;
	int m_exitCode = 1;
	int m_progressPercent = 0;
	int m_activityFrame = 0;
	std::wstring m_lastLogText;
	std::wstring m_statusText = L"Preparing update...";

	int collapsedWidth() const { return 560; }
	int collapsedHeight() const { return 180; }
	int expandedHeight() const { return 500; }

	static constexpr DWORD DwmUseImmersiveDarkModeLegacyAttribute = 19;
	static constexpr DWORD DwmUseImmersiveDarkModeAttribute = 20;
	static constexpr DWORD DwmBorderColorAttribute = 34;
	static constexpr DWORD DwmCaptionColorAttribute = 35;
	static constexpr DWORD DwmTextColorAttribute = 36;

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
		HBRUSH brush = CreateSolidBrush(fill);
		HPEN pen = CreatePen(PS_SOLID, 1, border);
		const HGDIOBJ oldBrush = SelectObject(hdc, brush);
		const HGDIOBJ oldPen = SelectObject(hdc, pen);
		RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
		SelectObject(hdc, oldBrush);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		DeleteObject(brush);
	}

	void drawFilledEllipse(HDC hdc, const RECT &rect, const COLORREF fill, const COLORREF border) const {
		HBRUSH brush = CreateSolidBrush(fill);
		HPEN pen = CreatePen(PS_SOLID, 1, border);
		const HGDIOBJ oldBrush = SelectObject(hdc, brush);
		const HGDIOBJ oldPen = SelectObject(hdc, pen);
		Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);
		SelectObject(hdc, oldBrush);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		DeleteObject(brush);
	}

	void drawStatusBadgeGlyph(HDC hdc, const RECT &badge) const {
		if (!m_completed) {
			const int dotSize = 5;
			const int gap = 4;
			const int totalWidth = dotSize * 3 + gap * 2;
			const int left = badge.left + ((badge.right - badge.left - totalWidth) / 2);
			const int top = badge.top + ((badge.bottom - badge.top - dotSize) / 2);
			for (int index = 0; index < 3; ++index) {
				const bool active = index == (m_activityFrame % 3);
				const RECT dot { left + index * (dotSize + gap), top, left + index * (dotSize + gap) + dotSize,
								 top + dotSize };
				const COLORREF dotColor = active ? m_theme.accentHover : mixColors(m_theme.surface2, m_theme.accent, 0.22);
				drawFilledEllipse(hdc, dot, dotColor, dotColor);
			}
			return;
		}

		const bool success = m_exitCode == 0 || static_cast< DWORD >(m_exitCode) == RestartRequired;
		if (success) {
			HPEN pen = CreatePen(PS_SOLID, 3, m_theme.success);
			const HGDIOBJ oldPen = SelectObject(hdc, pen);
			MoveToEx(hdc, badge.left + 9, badge.top + 17, nullptr);
			LineTo(hdc, badge.left + 14, badge.top + 22);
			LineTo(hdc, badge.left + 24, badge.top + 10);
			SelectObject(hdc, oldPen);
			DeleteObject(pen);
			return;
		}

		HPEN pen = CreatePen(PS_SOLID, 3, m_theme.danger);
		const HGDIOBJ oldPen = SelectObject(hdc, pen);
		MoveToEx(hdc, badge.left + 16, badge.top + 9, nullptr);
		LineTo(hdc, badge.left + 16, badge.top + 19);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
		const RECT dot { badge.left + 14, badge.top + 23, badge.left + 18, badge.top + 27 };
		drawFilledEllipse(hdc, dot, m_theme.danger, m_theme.danger);
	}

	void paintWindow() {
		PAINTSTRUCT paint {};
		HDC hdc = BeginPaint(m_hwnd, &paint);
		RECT client {};
		GetClientRect(m_hwnd, &client);
		FillRect(hdc, &client, m_backgroundBrush);

		RECT panel { 12, 12, client.right - 12, client.bottom - 12 };
		drawRoundedRect(hdc, panel, m_theme.base, mixColors(m_theme.surface1, m_theme.text, 0.08), 14);

		RECT accentStrip { panel.left, panel.top + 8, panel.left + 3, panel.bottom - 8 };
		fillRectColor(hdc, accentStrip, m_theme.accent);

		const int buttonTop = m_detailsVisible ? expandedHeight() - 56 : collapsedHeight() - 56;
		RECT actionDivider { panel.left + 1, buttonTop - 13, panel.right - 1, buttonTop - 12 };
		fillRectColor(hdc, actionDivider, mixColors(m_theme.surface1, m_theme.text, 0.045));

		RECT badge = m_badgeRect;
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
			RECT logRect {};
			GetWindowRect(m_log, &logRect);
			MapWindowPoints(nullptr, m_hwnd, reinterpret_cast< POINT * >(&logRect), 2);
			RECT frame { logRect.left - 1, logRect.top - 1, logRect.right + 1, logRect.bottom + 1 };
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

		RECT fill = track;
		const int trackWidth = track.right - track.left;
		if (m_indeterminate && !m_completed) {
			const int pulseWidth = std::max(70, trackWidth / 3);
			const int travel = trackWidth + pulseWidth;
			const int offset = (m_activityFrame * 22) % std::max(1, travel);
			fill.left = track.left + offset - pulseWidth;
			fill.right = fill.left + pulseWidth;
			fill.left = std::max(fill.left, track.left);
			fill.right = std::min(fill.right, track.right);
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
		const bool pressed = (item->itemState & ODS_SELECTED) != 0;
		const bool focused = (item->itemState & ODS_FOCUS) != 0;
		const bool primary = item->CtlID == ControlClose && m_completed && !disabled;
		COLORREF fill = primary ? m_theme.accent : m_theme.surface0;
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

		wchar_t text[128] {};
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
		m_panelBrush = CreateSolidBrush(m_theme.base);
		m_logBrush = CreateSolidBrush(m_theme.crust);

		m_uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
							   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
		m_titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
								  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
								  VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
		m_logFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
								CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
		if (!m_logFont) {
			m_logFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
									OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
									FIXED_PITCH | FF_MODERN, L"Consolas");
		}

		m_title = CreateWindowExW(0, L"STATIC", L"Installing Mumble update", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
								  m_hwnd, reinterpret_cast< HMENU >(ControlTitle), nullptr, nullptr);
		m_status = CreateWindowExW(0, L"STATIC", m_statusText.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd,
									reinterpret_cast< HMENU >(ControlStatus), nullptr, nullptr);
		m_detailsButton = CreateWindowExW(0, L"BUTTON", L"Show details",
										  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
										  reinterpret_cast< HMENU >(ControlDetails), nullptr, nullptr);
		m_closeButton = CreateWindowExW(0, L"BUTTON", L"Close",
										WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_OWNERDRAW, 0, 0, 0,
										0, m_hwnd, reinterpret_cast< HMENU >(ControlClose), nullptr, nullptr);
		m_log = CreateWindowExW(0, L"EDIT", nullptr,
								WS_CHILD | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL,
								0, 0, 0, 0, m_hwnd, reinterpret_cast< HMENU >(ControlLog), nullptr, nullptr);

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
		RECT client {};
		GetClientRect(m_hwnd, &client);
		const int width = client.right - client.left;
		const int margin = 22;
		const int buttonWidth = 118;
		const int buttonHeight = 34;
		const int badgeSize = 32;
		const int contentLeft = margin + badgeSize + 12;

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
		m_exitCode = static_cast< int >(exitCode);
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
	DWORD updateExitCode = packageMode ? runPackageUpdate(options) : runInstaller(options);
	bool installerRan = !packageMode;

	if (packageMode && !updateSucceeded(updateExitCode) && !updateCancelled(updateExitCode)
		&& fileExists(options.installerPath)) {
		appendLog(options, L"Package update failed with code " + std::to_wstring(updateExitCode)
							   + L"; running verified MSI fallback.");
		postUiProgress(-1, true);
		updateExitCode = runInstaller(options);
		installerRan = true;
	} else if (packageMode && updateCancelled(updateExitCode)) {
		appendLog(options, L"Package update was cancelled; MSI fallback will not run.");
	} else if (packageMode && !updateSucceeded(updateExitCode)) {
		appendLog(options, L"Package update failed and no verified MSI fallback was available.");
	}

	if (installerRan && !options.noRelaunch && updateSucceeded(updateExitCode)) {
		appendLog(options, L"Windows Installer completed; preparing to restart Mumble.");
		postUiProgress(100, false);
		Sleep(800);
		relaunchMumble(options);
	} else if (packageMode && updateCancelled(updateExitCode) && !options.noRelaunch) {
		appendLog(options, L"Update was cancelled; restarting Mumble without applying the MSI fallback.");
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
