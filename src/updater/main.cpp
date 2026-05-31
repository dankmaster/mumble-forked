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
	std::wstring packagePath;
	std::wstring appPath;
	std::wstring workingDirectory;
	std::wstring updaterLogPath;
	std::wstring msiLogPath;
	bool passive = true;
	bool noRelaunch = false;
	bool elevatedRetry = false;
};

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

std::wstring packageUpdaterArguments(const Options &options, const bool elevatedRetry) {
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
	return arguments;
}

DWORD relaunchElevatedForPackage(const Options &options) {
	const std::wstring executable = currentExecutablePath();
	const std::wstring parameters = packageUpdaterArguments(options, true);

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

	foreach ($file in $files) {
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

	New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
	try {
		foreach ($file in $files) {
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

	const bool packageMode = !options.packagePath.empty();
	const DWORD updateExitCode = packageMode ? runPackageUpdate(options) : runInstaller(options);
	if (!packageMode && !options.noRelaunch && (updateExitCode == 0 || updateExitCode == RestartRequired)) {
		Sleep(800);
		relaunchMumble(options);
	}

	appendLog(options, L"MumbleUpdater finished.");
	return static_cast< int >(updateExitCode);
}
