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
		[bool] $RequireHealth
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

	$manifestFiles = @(
		Get-ChildItem -LiteralPath (Join-Path $PackageRoot 'payload') -File -Recurse | ForEach-Object {
			$relative = [System.IO.Path]::GetRelativePath((Join-Path $PackageRoot 'payload'), $_.FullName).Replace('\', '/')
			[ordered]@{
				path = $relative
				size = $_.Length
				sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
			}
		}
	)
	$manifest = [ordered]@{
		format = 'mumble-update-v1'
		minUpdaterVersion = 3
		applyMode = 'replace-staged-payload'
		healthCheck = [ordered]@{
			required = $RequireHealth
			minimumStableRuntimeMilliseconds = 10000
			timeoutMilliseconds = 12000
		}
		files = $manifestFiles
	}
	Write-Utf8File -Path (Join-Path $PackageRoot 'manifest.json') -Content ($manifest | ConvertTo-Json -Depth 8)
	if (Test-Path -LiteralPath $PackagePath) {
		Remove-Item -LiteralPath $PackagePath -Force
	}
	Compress-Archive -LiteralPath (Join-Path $PackageRoot 'manifest.json'), (Join-Path $PackageRoot 'payload') `
		-DestinationPath $PackagePath -CompressionLevel Optimal
}

function Invoke-Updater {
	param(
		[string] $PackagePath,
		[string] $AppPath,
		[string] $UpdateRoot,
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
	$beforeInstallerAdmissionHash = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash
	foreach ($installerSha in @('', ('0' * 64))) {
		$installerExit = Invoke-InstallerAdmission -InstallerPath $fakeInstaller -InstallerSha256 $installerSha `
			-AppPath $appPath -UpdateRoot $updateRoot
		if ($installerExit -eq 0) {
			throw 'An MSI without its exact mandatory SHA256 unexpectedly passed updater admission.'
		}
	}
	if ((Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash -ne $beforeInstallerAdmissionHash -or
		(Get-TestRecoveryValues).Count -ne 0) {
		throw 'Failed MSI digest admission mutated application or recovery state.'
	}

	$knownGoodClientSource = Get-Item -LiteralPath (Join-Path $env:WINDIR 'System32\where.exe')
	$knownGoodPackage = Join-Path $testRoot 'known-good.zip'
	New-TestPackage -PackageRoot (Join-Path $testRoot 'known-good-package') -PackagePath $knownGoodPackage `
		-Files ([ordered]@{
			'mumble.exe' = $knownGoodClientSource
			'mumble-updater.exe' = 'known-good-updater'
			'stale-managed.dll' = 'known-good-stale-file'
		}) -RequireHealth $false

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

	$firstExit = Invoke-Updater -PackagePath $knownGoodPackage -AppPath $appPath -UpdateRoot $updateRoot -NoRelaunch
	if ($firstExit -ne 0) {
		throw "Known-good package apply failed with exit code $firstExit."
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
