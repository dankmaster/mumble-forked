# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $UpdaterPath,

	[Parameter(Mandatory = $true)]
	[string] $ProductUpdaterPath
)

$ErrorActionPreference = 'Stop'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('mumble-update-health-' + [System.Guid]::NewGuid().ToString('N'))

function Write-Utf8File {
	param([string] $Path, [string] $Content)
	$parent = Split-Path -Parent $Path
	[System.IO.Directory]::CreateDirectory($parent) | Out-Null
	[System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
}

function New-TestPackage {
	param(
		[string] $PackageRoot,
		[string] $PackagePath,
		[System.Collections.IDictionary] $Files,
		[System.Collections.IDictionary] $ManifestFiles = $null,
		[bool] $RequireHealth,
		[int] $MinUpdaterVersion = 4,
		[switch] $OmitMinUpdaterVersion,
		[switch] $OmitHealthCheck
	)

	if (Test-Path -LiteralPath $PackageRoot) {
		Remove-Item -LiteralPath $PackageRoot -Recurse -Force
	}
	[System.IO.Directory]::CreateDirectory((Join-Path $PackageRoot 'payload')) | Out-Null
	foreach ($entry in $Files.GetEnumerator()) {
		$target = Join-Path (Join-Path $PackageRoot 'payload') $entry.Key
		if ($entry.Value -is [System.IO.FileInfo]) {
			[System.IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
			[System.IO.File]::Copy($entry.Value.FullName, $target, $true)
		} else {
			Write-Utf8File -Path $target -Content ([string] $entry.Value)
		}
	}

	$manifestSourceRoot = Join-Path $PackageRoot 'payload'
	if ($null -ne $ManifestFiles) {
		$manifestSourceRoot = Join-Path $PackageRoot 'target-manifest'
		[System.IO.Directory]::CreateDirectory($manifestSourceRoot) | Out-Null
		foreach ($entry in $ManifestFiles.GetEnumerator()) {
			$target = Join-Path $manifestSourceRoot $entry.Key
			if ($entry.Value -is [System.IO.FileInfo]) {
				[System.IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
				[System.IO.File]::Copy($entry.Value.FullName, $target, $true)
			} else {
				Write-Utf8File -Path $target -Content ([string] $entry.Value)
			}
		}
	}
	$manifestEntries = @(
		Get-ChildItem -LiteralPath $manifestSourceRoot -File -Recurse | ForEach-Object {
			$relative = [System.IO.Path]::GetRelativePath($manifestSourceRoot, $_.FullName).Replace('\', '/')
			[ordered]@{
				path = $relative
				size = $_.Length
				sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
			}
		}
	)
	$manifest = [ordered]@{
		format = 'mumble-update-v1'
		minUpdaterVersion = $MinUpdaterVersion
		applyMode = 'replace-staged-payload'
		healthCheck = [ordered]@{
			required = $RequireHealth
			minimumStableRuntimeMilliseconds = 10000
			timeoutMilliseconds = 45000
		}
		files = $manifestEntries
	}
	if ($OmitMinUpdaterVersion) {
		$manifest.Remove('minUpdaterVersion')
	}
	if ($OmitHealthCheck) {
		$manifest.Remove('healthCheck')
	}
	Write-Utf8File -Path (Join-Path $PackageRoot 'manifest.json') -Content ($manifest | ConvertTo-Json -Depth 8)
	if (Test-Path -LiteralPath $PackagePath) {
		Remove-Item -LiteralPath $PackagePath -Force
	}
	Compress-Archive -LiteralPath (Join-Path $PackageRoot 'manifest.json'), (Join-Path $PackageRoot 'payload') `
		-DestinationPath $PackagePath -CompressionLevel Optimal
}

if (-not ('MumbleUpdateHealthTestKey' -as [type])) {
	Add-Type -TypeDefinition @'
using System;
using System.IO;

public static class MumbleUpdateHealthTestKey {
    public static string FromPath(string path) {
        string value = Path.GetFullPath(path).Replace('/', '\\');
        if (value.StartsWith("\\\\?\\UNC\\", StringComparison.OrdinalIgnoreCase)) {
            value = "\\\\" + value.Substring(8);
        } else if (value.StartsWith("\\\\?\\", StringComparison.OrdinalIgnoreCase)) {
            value = value.Substring(4);
        }
        value = value.ToLowerInvariant();
        unchecked {
            ulong hash = 14695981039346656037UL;
            foreach (char character in value) {
                hash ^= character;
                hash *= 1099511628211UL;
            }
            return hash.ToString("x16");
        }
    }
}
'@
}

function Set-TestInstalledPackageState {
	param(
		[string] $PackagePath,
		[string] $AppRoot,
		[string] $AppPath,
		[string] $UpdateRoot,
		[System.Collections.IDictionary] $Files
	)
	$records = @()
	foreach ($entry in $Files.GetEnumerator()) {
		$target = Join-Path $AppRoot $entry.Key
		[System.IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
		if ($entry.Value -is [System.IO.FileInfo]) {
			[System.IO.File]::Copy($entry.Value.FullName, $target, $true)
		} else {
			Write-Utf8File -Path $target -Content ([string] $entry.Value)
		}
		$file = Get-Item -LiteralPath $target
		$records += [ordered]@{
			path = ([string]$entry.Key).Replace('\', '/')
			size = [int64]$file.Length
			sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
		}
	}
	$key = [MumbleUpdateHealthTestKey]::FromPath($AppRoot)
	$manifestPath = Join-Path (Join-Path $UpdateRoot 'installed-manifests') "$key.json"
	$manifest = [ordered]@{
		schema = 1
		appPath = [System.IO.Path]::GetFullPath($AppPath)
		appDir = [System.IO.Path]::GetFullPath($AppRoot)
		packageIdentity = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
		files = @($records)
	}
	Write-Utf8File -Path $manifestPath -Content ($manifest | ConvertTo-Json -Depth 8)
}

function Invoke-Updater {
	param(
		[string] $PackagePath,
		[string] $AppPath,
		[string] $UpdateRoot,
		[string] $UpdaterExecutable = $UpdaterPath,
		[switch] $NoRelaunch,
		[string] $PackageSha256,
		[string[]] $ExtraArguments = @()
	)
	if ([string]::IsNullOrWhiteSpace($PackageSha256)) {
		$PackageSha256 = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
	}
	$arguments = @(
		'--package', $PackagePath,
		'--package-sha256', $PackageSha256,
		'--app', $AppPath,
		'--working-dir', (Split-Path -Parent $AppPath),
		'--updater-log', (Join-Path $UpdateRoot 'mumble-updater.log'),
		'--no-ui'
	)
	if ($NoRelaunch) {
		$arguments += '--no-relaunch'
	}
	$arguments += $ExtraArguments
	$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $UpdaterExecutable
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	foreach ($argument in $arguments) {
		$startInfo.ArgumentList.Add([string] $argument)
	}
	$process = [System.Diagnostics.Process]::Start($startInfo)
	$process.WaitForExit()
	return $process.ExitCode
}

function Invoke-UpdaterPrepare {
	param(
		[string] $PackagePath,
		[string] $AppPath,
		[string] $UpdateRoot
	)
	$packageSha256 = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()
	$arguments = @(
		'--package', $PackagePath,
		'--package-sha256', $packageSha256,
		'--app', $AppPath,
		'--working-dir', (Split-Path -Parent $AppPath),
		'--updater-log', (Join-Path $UpdateRoot 'mumble-updater.log'),
		'--prepare',
		'--no-ui'
	)
	$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $UpdaterPath
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	foreach ($argument in $arguments) {
		$startInfo.ArgumentList.Add([string] $argument)
	}
	$process = [System.Diagnostics.Process]::Start($startInfo)
	$process.WaitForExit()
	return $process.ExitCode
}

function Invoke-Recovery {
	param([string] $AppPath, [string] $UpdateRoot)
	$arguments = @(
		'--recover',
		'--app', $AppPath,
		'--working-dir', (Split-Path -Parent $AppPath),
		'--updater-log', (Join-Path $UpdateRoot 'mumble-updater.log'),
		'--no-ui',
		'--no-relaunch'
	)
	$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $UpdaterPath
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	foreach ($argument in $arguments) {
		$startInfo.ArgumentList.Add([string] $argument)
	}
	$process = [System.Diagnostics.Process]::Start($startInfo)
	$process.WaitForExit()
	return $process.ExitCode
}

function Invoke-InstallerAdmission {
	param(
		[string] $InstallerPath,
		[string] $InstallerSha256,
		[string] $RecoveryInstallerPath = '',
		[string] $RecoveryInstallerSha256 = '',
		[string] $CandidateExecutableSha256 = '',
		[string] $AppPath,
		[string] $UpdateRoot
	)
	$arguments = @(
		'--installer', $InstallerPath,
		'--app', $AppPath,
		'--working-dir', (Split-Path -Parent $AppPath),
		'--updater-log', (Join-Path $UpdateRoot 'mumble-updater.log'),
		'--no-ui',
		'--no-relaunch'
	)
	if (-not [string]::IsNullOrWhiteSpace($InstallerSha256)) {
		$arguments += @('--installer-sha256', $InstallerSha256)
	}
	if (-not [string]::IsNullOrWhiteSpace($RecoveryInstallerPath)) {
		$arguments += @('--recovery-installer', $RecoveryInstallerPath)
	}
	if (-not [string]::IsNullOrWhiteSpace($RecoveryInstallerSha256)) {
		$arguments += @('--recovery-installer-sha256', $RecoveryInstallerSha256)
	}
	if (-not [string]::IsNullOrWhiteSpace($CandidateExecutableSha256)) {
		$arguments += @('--candidate-executable-sha256', $CandidateExecutableSha256)
	}
	$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $UpdaterPath
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	foreach ($argument in $arguments) {
		$startInfo.ArgumentList.Add([string] $argument)
	}
	$process = [System.Diagnostics.Process]::Start($startInfo)
	$process.WaitForExit()
	return $process.ExitCode
}

function Assert-KnownGoodPayload {
	param([string] $AppRoot, [string] $AppPath, [System.IO.FileInfo] $KnownGoodClientSource)
	if ((Get-FileHash -LiteralPath $AppPath -Algorithm SHA256).Hash -ne `
		(Get-FileHash -LiteralPath $KnownGoodClientSource.FullName -Algorithm SHA256).Hash) {
		throw 'Client executable was not restored from the known-good payload.'
	}
	if ([System.IO.File]::ReadAllText((Join-Path $AppRoot 'mumble-updater.exe')) -ne 'known-good-updater') {
		throw 'Updater executable was not restored from the known-good payload.'
	}
	if ([System.IO.File]::ReadAllText((Join-Path $AppRoot 'stale-managed.dll')) -ne 'known-good-stale-file') {
		throw 'Stale managed file was not restored during rollback.'
	}
	if (Test-Path -LiteralPath (Join-Path $AppRoot 'new-managed.dll')) {
		throw 'New managed file was not removed during rollback.'
	}
}

function Wait-AutomaticRecovery {
	param([string] $UpdateRoot, [int] $TimeoutMilliseconds = 10000)
	$deadline = [Environment]::TickCount64 + $TimeoutMilliseconds
	do {
		$pending = @()
		$pendingRoot = Join-Path $UpdateRoot 'pending-health'
		if (Test-Path -LiteralPath $pendingRoot -PathType Container) {
			$pending = @(Get-ChildItem -LiteralPath $pendingRoot -File)
		}
		if ($pending.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 50
	} while ([Environment]::TickCount64 -lt $deadline)
	$watchdogLog = Get-Content -LiteralPath (Join-Path $UpdateRoot 'mumble-updater.log') -Raw -ErrorAction SilentlyContinue
	throw "The detached recovery watchdog did not consume the pending journal in time.`n$watchdogLog"
}

function Get-TestRecoveryValues {
	$runPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
	if (-not (Test-Path -LiteralPath $runPath)) {
		return @()
	}
	$item = Get-ItemProperty -LiteralPath $runPath
	return @($item.PSObject.Properties | Where-Object {
		$_.Name -like 'MumbleUpdateRecovery-*' -and ([string] $_.Value) -like ('*' + $testRoot + '*')
	})
}

try {
	if (-not (Test-Path -LiteralPath $UpdaterPath -PathType Leaf)) {
		throw "Updater does not exist: $UpdaterPath"
	}
	if (-not (Test-Path -LiteralPath $ProductUpdaterPath -PathType Leaf)) {
		throw "Product updater does not exist: $ProductUpdaterPath"
	}
	$crashHook = '--test-crash-after-journal'
	$productStrings = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($ProductUpdaterPath))
	$testStrings = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($UpdaterPath))
	if ($productStrings.Contains($crashHook) -or -not $testStrings.Contains($crashHook)) {
		throw 'Crash-injection hooks must exist only in the non-installed updater test binary.'
	}

	$appRoot = Join-Path $testRoot 'app'
	$updateRoot = Join-Path $testRoot 'Updates'
	[System.IO.Directory]::CreateDirectory($appRoot) | Out-Null
	[System.IO.Directory]::CreateDirectory($updateRoot) | Out-Null
	$appPath = Join-Path $appRoot 'mumble.exe'
	Write-Utf8File -Path $appPath -Content 'legacy-bootstrap'
	$fakeInstaller = Join-Path $testRoot 'untrusted.msi'
	Write-Utf8File -Path $fakeInstaller -Content 'not-an-msi'
	$fakeRecoveryInstaller = Join-Path $testRoot 'untrusted-recovery.msi'
	Write-Utf8File -Path $fakeRecoveryInstaller -Content 'not-a-recovery-msi'
	$beforeInstallerAdmissionHash = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash
	foreach ($installerSha in @('', ('0' * 64))) {
		$installerExit = Invoke-InstallerAdmission -InstallerPath $fakeInstaller -InstallerSha256 $installerSha `
			-AppPath $appPath -UpdateRoot $updateRoot
		if ($installerExit -eq 0) {
			throw 'An MSI without its exact mandatory SHA256 unexpectedly passed updater admission.'
		}
	}
	$fakeInstallerSha = (Get-FileHash -LiteralPath $fakeInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
	$candidateExecutableSha = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$missingRecoveryDigestExit = Invoke-InstallerAdmission -InstallerPath $fakeInstaller `
		-InstallerSha256 $fakeInstallerSha -RecoveryInstallerPath $fakeRecoveryInstaller `
		-AppPath $appPath -UpdateRoot $updateRoot
	if ($missingRecoveryDigestExit -eq 0) {
		throw 'A recovery MSI without its paired digest unexpectedly passed updater admission.'
	}
	$badRecoveryDigestExit = Invoke-InstallerAdmission -InstallerPath $fakeInstaller `
		-InstallerSha256 $fakeInstallerSha -RecoveryInstallerPath $fakeRecoveryInstaller `
		-RecoveryInstallerSha256 ('0' * 64) -CandidateExecutableSha256 $candidateExecutableSha `
		-AppPath $appPath -UpdateRoot $updateRoot
	if ($badRecoveryDigestExit -eq 0) {
		throw 'A recovery MSI with the wrong mandatory digest unexpectedly passed updater admission.'
	}
	$fakeRecoveryInstallerSha = (Get-FileHash -LiteralPath $fakeRecoveryInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
	$missingExecutableDigestExit = Invoke-InstallerAdmission -InstallerPath $fakeInstaller `
		-InstallerSha256 $fakeInstallerSha -RecoveryInstallerPath $fakeRecoveryInstaller `
		-RecoveryInstallerSha256 $fakeRecoveryInstallerSha -AppPath $appPath -UpdateRoot $updateRoot
	if ($missingExecutableDigestExit -eq 0) {
		throw 'An MSI probation request without its candidate executable digest unexpectedly passed admission.'
	}
	if ((Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash -ne $beforeInstallerAdmissionHash -or
		(Get-TestRecoveryValues).Count -ne 0) {
		throw 'Failed MSI digest admission mutated application or recovery state.'
	}

	# Protocol-v4 native packages must never bypass health probation through a
	# missing/old version or an absent/false health contract. Exercise the real
	# product updater, not the crash-injection test binary, and prove rejection
	# happens before any application or recovery state changes.
	$invalidFiles = [ordered]@{
		'mumble.exe' = 'invalid-candidate'
		'mumble-updater.exe' = 'invalid-updater'
	}
	$noHealthPackage = Join-Path $testRoot 'invalid-no-health.zip'
	New-TestPackage -PackageRoot (Join-Path $testRoot 'invalid-no-health-package') `
		-PackagePath $noHealthPackage -Files $invalidFiles -RequireHealth $false
	$oldProtocolPackage = Join-Path $testRoot 'invalid-old-protocol.zip'
	New-TestPackage -PackageRoot (Join-Path $testRoot 'invalid-old-protocol-package') `
		-PackagePath $oldProtocolPackage -Files $invalidFiles -RequireHealth $true -MinUpdaterVersion 3
	$missingHealthPackage = Join-Path $testRoot 'invalid-missing-health.zip'
	New-TestPackage -PackageRoot (Join-Path $testRoot 'invalid-missing-health-package') `
		-PackagePath $missingHealthPackage -Files $invalidFiles -RequireHealth $true -OmitHealthCheck
	$missingProtocolPackage = Join-Path $testRoot 'invalid-missing-protocol.zip'
	New-TestPackage -PackageRoot (Join-Path $testRoot 'invalid-missing-protocol-package') `
		-PackagePath $missingProtocolPackage -Files $invalidFiles -RequireHealth $true -OmitMinUpdaterVersion
	$beforeInvalidManifestHash = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash
	foreach ($invalidPackage in @(
		$noHealthPackage, $oldProtocolPackage, $missingHealthPackage, $missingProtocolPackage
	)) {
		$invalidExit = Invoke-Updater -PackagePath $invalidPackage -AppPath $appPath -UpdateRoot $updateRoot `
			-UpdaterExecutable $ProductUpdaterPath -NoRelaunch
		if ($invalidExit -eq 0) {
			throw "Invalid protocol-v4 package unexpectedly passed admission: $invalidPackage"
		}
		if ((Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash -ne $beforeInvalidManifestHash -or
			(Get-TestRecoveryValues).Count -ne 0) {
			throw "Invalid protocol-v4 package mutated application or recovery state: $invalidPackage"
		}
	}

	$knownGoodClientSource = Get-Item -LiteralPath (Join-Path $env:WINDIR 'System32\where.exe')
	$knownGoodPackage = Join-Path $testRoot 'known-good.zip'
	$knownGoodFiles = [ordered]@{
		'mumble.exe' = $knownGoodClientSource
		'mumble-updater.exe' = 'known-good-updater'
		'stale-managed.dll' = 'known-good-stale-file'
	}
	New-TestPackage -PackageRoot (Join-Path $testRoot 'known-good-package') -PackagePath $knownGoodPackage `
		-Files $knownGoodFiles -RequireHealth $true
	Set-TestInstalledPackageState -PackagePath $knownGoodPackage -AppRoot $appRoot -AppPath $appPath `
		-UpdateRoot $updateRoot -Files $knownGoodFiles

	# A sparse archive carries the complete target manifest but only files that
	# differ from its known base. Prove the real updater prepares it when the
	# unchanged installed client matches, and fails closed before mutation when
	# that omitted base file does not match.
	$sparsePackage = Join-Path $testRoot 'sparse.zip'
	$sparseTargetFiles = [ordered]@{
		'mumble.exe' = $knownGoodClientSource
		'mumble-updater.exe' = 'sparse-updater'
		'new-managed.dll' = 'sparse-new-file'
	}
	$sparsePayloadFiles = [ordered]@{
		'mumble-updater.exe' = 'sparse-updater'
		'new-managed.dll' = 'sparse-new-file'
	}
	New-TestPackage -PackageRoot (Join-Path $testRoot 'sparse-package') -PackagePath $sparsePackage `
		-Files $sparsePayloadFiles -ManifestFiles $sparseTargetFiles -RequireHealth $true
	$sparsePrepareExit = Invoke-UpdaterPrepare -PackagePath $sparsePackage -AppPath $appPath -UpdateRoot $updateRoot
	if ($sparsePrepareExit -ne 0) {
		throw "Sparse package preparation failed with exit $sparsePrepareExit."
	}
	$sparseIdentity = (Get-FileHash -LiteralPath $sparsePackage -Algorithm SHA256).Hash.ToLowerInvariant()
	$sparseStageRoot = Join-Path (Join-Path $updateRoot 'prepared-packages') $sparseIdentity
	if (Test-Path -LiteralPath (Join-Path $sparseStageRoot 'payload\mumble.exe')) {
		throw 'Sparse package unexpectedly staged an unchanged file that was absent from its payload.'
	}
	foreach ($changedPath in @('mumble-updater.exe', 'new-managed.dll')) {
		if (-not (Test-Path -LiteralPath (Join-Path $sparseStageRoot "payload\$changedPath") -PathType Leaf)) {
			throw "Sparse package did not stage changed file '$changedPath'."
		}
	}

	$knownGoodClientBackup = Join-Path $testRoot 'known-good-client-backup.exe'
	Copy-Item -LiteralPath $appPath -Destination $knownGoodClientBackup -Force
	Write-Utf8File -Path $appPath -Content 'locally-diverged-client'
	$beforeSparseMismatchHash = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash
	$sparseMismatchExit = Invoke-UpdaterPrepare -PackagePath $sparsePackage -AppPath $appPath -UpdateRoot $updateRoot
	if ($sparseMismatchExit -eq 0) {
		throw 'Sparse package unexpectedly prepared against a mismatched omitted base file.'
	}
	if ((Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash -ne $beforeSparseMismatchHash) {
		throw 'Sparse base mismatch mutated the installed application during prepare.'
	}
	Copy-Item -LiteralPath $knownGoodClientBackup -Destination $appPath -Force

	# The package digest is an admission credential, not descriptive metadata.
	# A mismatch must fail before any application or recovery state is mutated.
	$beforeDigestMismatchHash = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash
	$digestMismatchExit = Invoke-Updater -PackagePath $knownGoodPackage -AppPath $appPath -UpdateRoot $updateRoot `
		-NoRelaunch -PackageSha256 ('0' * 64)
	if ($digestMismatchExit -eq 0) {
		throw 'A package with a mismatched mandatory SHA256 unexpectedly passed admission.'
	}
	if ((Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash -ne $beforeDigestMismatchHash) {
		throw 'The application changed after package SHA256 admission failed.'
	}
	if ((Get-TestRecoveryValues).Count -ne 0) {
		throw 'Package SHA256 admission failure armed persistent recovery state.'
	}

	$brokenPackage = Join-Path $testRoot 'broken.zip'
	New-TestPackage -PackageRoot (Join-Path $testRoot 'broken-package') -PackagePath $brokenPackage `
		-Files ([ordered]@{
			# A valid executable that exits immediately without a health marker.
			'mumble.exe' = Get-Item -LiteralPath $UpdaterPath
			'mumble-updater.exe' = 'new-updater'
			'new-managed.dll' = 'must-be-removed-by-rollback'
		}) -RequireHealth $true

	$noRelaunchHealthExit = Invoke-Updater -PackagePath $brokenPackage -AppPath $appPath -UpdateRoot $updateRoot `
		-NoRelaunch -ExtraArguments @('--test-skip-recovery-watchdog')
	if ($noRelaunchHealthExit -eq 0) {
		throw 'A health-required package unexpectedly succeeded with --no-relaunch.'
	}
	Assert-KnownGoodPayload -AppRoot $appRoot -AppPath $appPath -KnownGoodClientSource $knownGoodClientSource
	if ((Get-TestRecoveryValues).Count -ne 0) {
		throw 'No-relaunch health rollback left a persistent recovery trigger behind.'
	}
	$fallbackRequestPath = Join-Path (Join-Path $updateRoot 'installer-fallback') `
		("$([MumbleUpdateHealthTestKey]::FromPath($appRoot)).json")
	$fallbackRequest = Get-Content -LiteralPath $fallbackRequestPath -Raw | ConvertFrom-Json
	$brokenIdentity = (Get-FileHash -LiteralPath $brokenPackage -Algorithm SHA256).Hash.ToLowerInvariant()
	if ([int]$fallbackRequest.schema -ne 1 -or
		[string]$fallbackRequest.packageIdentity -cne $brokenIdentity -or
		[int64]$fallbackRequest.updateExitCode -le 0) {
		throw ("Safe native package failure did not persist the exact lazy MSI fallback request. schema={0}, package={1}, expectedPackage={2}, exit={3}" -f $fallbackRequest.schema, $fallbackRequest.packageIdentity, $brokenIdentity, $fallbackRequest.updateExitCode)
	}

	# Crash immediately after the durable journal is committed. No application
	# file may have changed, and a recovery-only invocation must consume the
	# journal without needing the original package.
	$beforeJournalCrashHash = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash
	$journalCrashExit = Invoke-Updater -PackagePath $brokenPackage -AppPath $appPath -UpdateRoot $updateRoot `
		-NoRelaunch -ExtraArguments @('--test-crash-after-journal', '--test-skip-recovery-watchdog')
	if ($journalCrashExit -eq 0) {
		throw 'The after-journal crash hook unexpectedly returned success.'
	}
	if ((Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash -ne $beforeJournalCrashHash) {
		throw 'An application file changed before the durable pending journal boundary.'
	}
	if ((Get-TestRecoveryValues).Count -ne 1) {
		throw 'The durable journal boundary did not leave exactly one persistent recovery trigger armed.'
	}
	$journalRecoveryExit = Invoke-Recovery -AppPath $appPath -UpdateRoot $updateRoot
	if ($journalRecoveryExit -ne 0) {
		$recoveryLog = Get-Content -LiteralPath (Join-Path $updateRoot 'mumble-updater.log') -Raw -ErrorAction SilentlyContinue
		throw "Recovery-only mode failed after the journal-before-mutation crash with exit $journalRecoveryExit.`n$recoveryLog"
	}
	Assert-KnownGoodPayload -AppRoot $appRoot -AppPath $appPath -KnownGoodClientSource $knownGoodClientSource
	if ((Get-TestRecoveryValues).Count -ne 0) {
		throw 'Explicit recovery left a persistent recovery trigger behind.'
	}

	# Crash after exactly one managed-file mutation. This deliberately leaves a
	# mixed payload; the detached watchdog must recover it without another
	# updater invocation or a reboot.
	$mutationCrashExit = Invoke-Updater -PackagePath $brokenPackage -AppPath $appPath -UpdateRoot $updateRoot `
		-NoRelaunch -ExtraArguments @('--test-crash-after-first-mutation')
	if ($mutationCrashExit -eq 0) {
		throw 'The first-mutation crash hook unexpectedly returned success.'
	}
	Wait-AutomaticRecovery -UpdateRoot $updateRoot
	Assert-KnownGoodPayload -AppRoot $appRoot -AppPath $appPath -KnownGoodClientSource $knownGoodClientSource
	if ((Get-TestRecoveryValues).Count -ne 0) {
		throw 'Automatic recovery left a persistent recovery trigger behind.'
	}

	$brokenExit = Invoke-Updater -PackagePath $brokenPackage -AppPath $appPath -UpdateRoot $updateRoot
	if ($brokenExit -eq 0) {
		throw 'Broken payload unexpectedly passed controlled restart qualification.'
	}
	Assert-KnownGoodPayload -AppRoot $appRoot -AppPath $appPath -KnownGoodClientSource $knownGoodClientSource
	if (Test-Path -LiteralPath (Join-Path $updateRoot 'pending-health') -PathType Container) {
		$pending = @(Get-ChildItem -LiteralPath (Join-Path $updateRoot 'pending-health') -File)
		if ($pending.Count -ne 0) {
			throw 'Pending health state remained after successful rollback.'
		}
	}

	$installedManifest = Get-ChildItem -LiteralPath (Join-Path $updateRoot 'installed-manifests') -File | Select-Object -First 1
	$installed = Get-Content -LiteralPath $installedManifest.FullName -Raw | ConvertFrom-Json
	$knownGoodIdentity = (Get-FileHash -LiteralPath $knownGoodPackage -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($installed.packageIdentity -ne $knownGoodIdentity) {
		throw 'Installed manifest did not roll back to the known-good package identity.'
	}

	$log = Get-Content -LiteralPath (Join-Path $updateRoot 'mumble-updater.log') -Raw
	if ($log -notmatch 'Restored the last known-good immutable application payload') {
		throw 'Updater log does not attest the rollback.'
	}
	if ((Get-TestRecoveryValues).Count -ne 0) {
		throw 'Controlled rollback left a persistent recovery trigger behind.'
	}
} finally {
	# Never leave a test-owned persistent Run value behind if an assertion interrupts a
	# crash-window scenario before the normal recovery invocation clears it.
	$runPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
	if (Test-Path -LiteralPath $runPath) {
		$item = Get-ItemProperty -LiteralPath $runPath
		foreach ($property in $item.PSObject.Properties) {
			if ($property.Name -like 'MumbleUpdateRecovery-*' -and ([string] $property.Value) -like ('*' + $testRoot + '*')) {
				Remove-ItemProperty -LiteralPath $runPath -Name $property.Name -Force -ErrorAction SilentlyContinue
			}
		}
	}
	if (Test-Path -LiteralPath $testRoot) {
		for ($attempt = 0; $attempt -lt 5 -and (Test-Path -LiteralPath $testRoot); $attempt++) {
			Start-Sleep -Milliseconds 100
			Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
		}
	}
}
