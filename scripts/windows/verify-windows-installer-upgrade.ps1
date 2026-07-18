[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$PreviousClientMsi,
	[Parameter(Mandatory = $true)][string]$CandidateClientMsi,
	[Parameter(Mandatory = $true)][string]$CandidateManifestPath,
	[Parameter(Mandatory = $true)][string]$InstalledExecutablePath,
	[string]$ConfigPath = '',
	[string]$DatabasePath = '',
	[string]$ProfileSeedRoot = '',
	[string]$MsiPayloadEvidencePath = '',
	[Parameter(Mandatory = $true)][string]$OutputPath,
	[switch]$ContractOnly,
	[switch]$DisposableRunner,
	[int]$StartupTimeoutSeconds = 90,
	[int]$AutomationPort = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\WindowsMsiPayload.Common.psm1" -Force

$gateId = 'windows-qml-installer-upgrade'
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)

function Write-InstallerEvidenceJson {
	param([Parameter(Mandatory = $true)]$Document)

	$outputDirectory = Split-Path -Parent $outputFullPath
	if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
		New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
	}
	$temporaryPath = "$outputFullPath.$([Guid]::NewGuid().ToString('N')).tmp"
	try {
		$json = $Document | ConvertTo-Json -Depth 30
		[IO.File]::WriteAllText($temporaryPath, $json + [Environment]::NewLine,
			[Text.UTF8Encoding]::new($false))
		Move-Item -LiteralPath $temporaryPath -Destination $outputFullPath -Force
	} finally {
		Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
	}
}

function Assert-ProfileArguments {
	$hasRoot = -not [string]::IsNullOrWhiteSpace($ProfileSeedRoot)
	$hasConfig = -not [string]::IsNullOrWhiteSpace($ConfigPath)
	$hasDatabase = -not [string]::IsNullOrWhiteSpace($DatabasePath)
	if ($hasRoot -eq ($hasConfig -or $hasDatabase)) {
		throw 'Specify either ProfileSeedRoot or both ConfigPath and DatabasePath.'
	}
	if (-not $hasRoot -and -not ($hasConfig -and $hasDatabase)) {
		throw 'ConfigPath and DatabasePath must be supplied together.'
	}
}

Assert-ProfileArguments

if ($ContractOnly) {
	$contract = [ordered]@{
		schema_version = 1
		artifact_kind = 'windows_installer_upgrade_contract'
		gate_id = $gateId
		contract_only = $true
		status = 'contract_only'
		eligible = $false
		generated_at_utc = [DateTime]::UtcNow.ToString('o')
		required_inputs = @(
			'PreviousClientMsi', 'CandidateClientMsi', 'CandidateManifestPath',
			'InstalledExecutablePath', 'ConfigPath+DatabasePath|ProfileSeedRoot', 'OutputPath'
		)
		runtime_requirements = @(
			'Windows administrator token', 'explicit DisposableRunner',
			'Windows Installer COM', 'candidate MSI embedded executable SHA-256 preflight',
			'MUMBLE_MODERN_AUTOMATION readiness endpoint'
		)
		mutating_steps = @(
			'silent previous MSI install', 'silent candidate MSI upgrade',
			'candidate runtime readiness', 'silent candidate uninstall'
		)
	}
	Write-InstallerEvidenceJson -Document $contract
	$contract | ConvertTo-Json -Depth 10
	return
}

function Get-RequiredFile {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Label)
	$resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
	if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "$Label is not a file: $resolved" }
	return $resolved
}

function Get-FileSha256 {
	param([Parameter(Mandatory = $true)][string]$Path)
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ObjectPropertyValue {
	param([AllowNull()]$Object, [Parameter(Mandatory = $true)][string]$Name, $DefaultValue = $null)
	if ($null -eq $Object) { return $DefaultValue }
	$property = $Object.PSObject.Properties[$Name]
	return $property ? $property.Value : $DefaultValue
}

function Test-IsAdministrator {
	$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
	$principal = [Security.Principal.WindowsPrincipal]::new($identity)
	return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Release-ComObject {
	param([AllowNull()]$Value)
	if ($null -ne $Value -and [Runtime.InteropServices.Marshal]::IsComObject($Value)) {
		[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($Value)
	}
}

function Get-MsiProperty {
	param(
		[Parameter(Mandatory = $true)]$Database,
		[Parameter(Mandatory = $true)][string]$PropertyName
	)
	$view = $null
	$record = $null
	try {
		$query = "SELECT ``Value`` FROM ``Property`` WHERE ``Property``='$PropertyName'"
		$view = $Database.OpenView($query)
		$view.Execute()
		$record = $view.Fetch()
		if ($null -eq $record) { throw "MSI does not define required property $PropertyName." }
		return [string]$record.StringData(1)
	} finally {
		if ($null -ne $view) {
			try { $view.Close() } catch { }
		}
		Release-ComObject -Value $record
		Release-ComObject -Value $view
	}
}

function Get-MsiMetadata {
	param([Parameter(Mandatory = $true)][string]$Path)
	$installer = $null
	$database = $null
	try {
		$installer = New-Object -ComObject WindowsInstaller.Installer
		$database = $installer.OpenDatabase($Path, 0)
		$productCode = (Get-MsiProperty -Database $database -PropertyName 'ProductCode').ToUpperInvariant()
		$upgradeCode = (Get-MsiProperty -Database $database -PropertyName 'UpgradeCode').ToUpperInvariant()
		$productName = Get-MsiProperty -Database $database -PropertyName 'ProductName'
		$productVersion = Get-MsiProperty -Database $database -PropertyName 'ProductVersion'
		$guidPattern = '^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$'
		if ($productCode -notmatch $guidPattern -or $upgradeCode -notmatch $guidPattern) {
			throw "MSI ProductCode or UpgradeCode is not a canonical GUID: $Path"
		}
		return [ordered]@{
			path = $Path
			sha256 = Get-FileSha256 -Path $Path
			product_code = $productCode
			upgrade_code = $upgradeCode
			product_name = $productName
			product_version = $productVersion
		}
	} finally {
		Release-ComObject -Value $database
		Release-ComObject -Value $installer
	}
}

function Get-MsiProductState {
	param([Parameter(Mandatory = $true)][string]$ProductCode)
	$installer = $null
	try {
		$installer = New-Object -ComObject WindowsInstaller.Installer
		return [int]$installer.ProductState($ProductCode)
	} finally {
		Release-ComObject -Value $installer
	}
}

function Test-MsiProductInstalled {
	param([Parameter(Mandatory = $true)][string]$ProductCode)
	# INSTALLSTATE_DEFAULT (5) is a fully installed product. Advertised or absent
	# registrations do not satisfy the release gate.
	return (Get-MsiProductState -ProductCode $ProductCode) -eq 5
}

function Invoke-MsiOperation {
	param(
		[Parameter(Mandatory = $true)][ValidateSet('install', 'uninstall')][string]$Operation,
		[Parameter(Mandatory = $true)][string]$Target,
		[Parameter(Mandatory = $true)][string]$LogPath,
		[switch]$CleanupAttempt
	)
	$msiexec = Join-Path $env:SystemRoot 'System32\msiexec.exe'
	$verb = $Operation -eq 'install' ? '/i' : '/x'
	$arguments = @($verb, ('"{0}"' -f $Target), '/qn', '/norestart', '/l*v', ('"{0}"' -f $LogPath))
	$process = Start-Process -FilePath $msiexec -ArgumentList $arguments -Wait -PassThru
	$accepted = $process.ExitCode -in @(0, 3010)
	$result = [ordered]@{
		operation = $Operation
		target = $Target
		exit_code = [int]$process.ExitCode
		reboot_required = $process.ExitCode -eq 3010
		accepted = $accepted
		cleanup_attempt = [bool]$CleanupAttempt
		log_path = $LogPath
		log_sha256 = if (Test-Path -LiteralPath $LogPath -PathType Leaf) { Get-FileSha256 -Path $LogPath } else { $null }
	}
	if (-not $accepted -and -not $CleanupAttempt) {
		throw "msiexec $Operation failed with exit code $($process.ExitCode). Log: $LogPath"
	}
	return $result
}

function Get-ProfileDefinition {
	if (-not [string]::IsNullOrWhiteSpace($ProfileSeedRoot)) {
		$root = (Resolve-Path -LiteralPath $ProfileSeedRoot -ErrorAction Stop).Path
		if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "ProfileSeedRoot is not a directory: $root" }
		$config = Get-RequiredFile -Path (Join-Path $root 'mumble_settings.json') -Label 'Profile seed config'
		$database = Get-RequiredFile -Path (Join-Path $root 'mumble.sqlite') -Label 'Profile seed database'
		$files = @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object { $_.FullName })
		return [ordered]@{ mode = 'root'; root = $root; config = $config; database = $database; files = $files }
	}
	$config = Get-RequiredFile -Path $ConfigPath -Label 'ConfigPath'
	$database = Get-RequiredFile -Path $DatabasePath -Label 'DatabasePath'
	return [ordered]@{ mode = 'explicit'; root = $null; config = $config; database = $database; files = @($config, $database) }
}

function Get-ProfileSnapshot {
	param([Parameter(Mandatory = $true)]$Profile)
	$rows = @($Profile.files | ForEach-Object {
		$path = [string]$_
		if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Profile seed file disappeared: $path" }
		$identity = if ($Profile.mode -eq 'root') {
			$path.Substring(([string]$Profile.root).TrimEnd('\').Length + 1).Replace('\', '/')
		} elseif ([string]::Equals($path, [string]$Profile.config, [StringComparison]::OrdinalIgnoreCase)) {
			'config'
		} else { 'database' }
		[ordered]@{ path = $identity; size = [int64](Get-Item -LiteralPath $path).Length; sha256 = Get-FileSha256 -Path $path }
	} | Sort-Object path)
	$payload = $rows | ConvertTo-Json -Depth 5 -Compress
	$sha = [Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($payload))
	return [ordered]@{
		file_count = $rows.Count
		manifest_sha256 = ([BitConverter]::ToString($sha)).Replace('-', '').ToLowerInvariant()
		files = $rows
	}
}

function Test-ProfileSnapshotEqual {
	param([Parameter(Mandatory = $true)]$Before, [Parameter(Mandatory = $true)]$After)
	return [string]$Before.manifest_sha256 -ceq [string]$After.manifest_sha256
}

function Get-FreeTcpPort {
	$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
}

function Invoke-AutomationCommand {
	param([int]$Port, [string]$Token, [hashtable]$Request, [int]$TimeoutMilliseconds = 2000)
	$Request.token = $Token
	$client = [Net.Sockets.TcpClient]::new()
	$pending = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
	if (-not $pending.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
		$client.Dispose()
		throw "Timed out connecting to automation port $Port."
	}
	$client.EndConnect($pending)
	try {
		$client.ReceiveTimeout = $TimeoutMilliseconds
		$client.SendTimeout = $TimeoutMilliseconds
		$stream = $client.GetStream()
		$writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
		$reader = [IO.StreamReader]::new($stream, [Text.UTF8Encoding]::new($false))
		try {
			$writer.NewLine = "`n"
			$writer.WriteLine(($Request | ConvertTo-Json -Depth 10 -Compress))
			$writer.Flush()
			$line = $reader.ReadLine()
			if ([string]::IsNullOrWhiteSpace($line)) { throw 'Automation returned an empty response.' }
			$response = $line | ConvertFrom-Json
			if (-not [bool]$response.ok) { throw "Automation command '$($Request.command)' failed." }
			return $response
		} finally { $writer.Dispose(); $reader.Dispose() }
	} finally { $client.Dispose() }
}

function Wait-QmlRuntimeReady {
	param([int]$Port, [string]$Token, [Diagnostics.Process]$Process, [int]$TimeoutSeconds)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$lastState = $null
	do {
		$Process.Refresh()
		if ($Process.HasExited) { throw "Installed candidate exited before readiness with code $($Process.ExitCode)." }
		try {
			Invoke-AutomationCommand -Port $Port -Token $Token -Request @{ command = 'ping' } -TimeoutMilliseconds 1000 | Out-Null
			$lastState = Invoke-AutomationCommand -Port $Port -Token $Token -Request @{ command = 'qmlReadinessState' } -TimeoutMilliseconds 2000
			$names = @($lastState.PSObject.Properties.Name)
			if ($names -contains 'frontend' -and [string]$lastState.frontend -ceq 'qml' -and
				$names -contains 'windowReady' -and [bool]$lastState.windowReady -and
				$names -contains 'mainCaptureReady' -and [bool]$lastState.mainCaptureReady) {
				return $lastState
			}
		} catch { }
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	$diagnostic = $null -eq $lastState ? 'no readiness response' : ($lastState | ConvertTo-Json -Depth 8 -Compress)
	throw "Timed out waiting for installed QML candidate readiness. Last state: $diagnostic"
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
	throw 'The installer upgrade gate can run only on Windows.'
}
if (-not $DisposableRunner) {
	throw 'A real installer gate requires -DisposableRunner on a disposable Windows runner or VM.'
}
if (-not (Test-IsAdministrator)) {
	throw 'The installer upgrade gate requires an elevated administrator token.'
}

$previousMsiPath = Get-RequiredFile -Path $PreviousClientMsi -Label 'PreviousClientMsi'
$candidateMsiPath = Get-RequiredFile -Path $CandidateClientMsi -Label 'CandidateClientMsi'
$candidateManifestFullPath = Get-RequiredFile -Path $CandidateManifestPath -Label 'CandidateManifestPath'
$providedMsiPayloadEvidenceFullPath = if ([string]::IsNullOrWhiteSpace($MsiPayloadEvidencePath)) {
	$null
} else {
	Get-RequiredFile -Path $MsiPayloadEvidencePath -Label 'MsiPayloadEvidencePath'
}
$installedExecutableFullPath = [IO.Path]::GetFullPath($InstalledExecutablePath)
$profile = Get-ProfileDefinition
if ($profile.files.Count -eq 0) { throw 'The isolated profile seed contains no files.' }
if ($profile.mode -eq 'root' -and $outputFullPath.StartsWith(([string]$profile.root).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
	throw 'OutputPath must be outside ProfileSeedRoot.'
}
foreach ($protectedPath in @($previousMsiPath, $candidateMsiPath, $candidateManifestFullPath,
		$providedMsiPayloadEvidenceFullPath, $profile.config, $profile.database) | Where-Object { $null -ne $_ }) {
	if ([string]::Equals($outputFullPath, $protectedPath, [StringComparison]::OrdinalIgnoreCase)) {
		throw "OutputPath must not overwrite an input: $protectedPath"
	}
}

$outputDirectory = Split-Path -Parent $outputFullPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$runId = [Guid]::NewGuid().ToString('N')
$workRoot = Join-Path $outputDirectory "installer-upgrade-$runId"
$logRoot = Join-Path $workRoot 'logs'
$runtimeProfileRoot = Join-Path $workRoot 'runtime-profile'
New-Item -ItemType Directory -Force -Path $logRoot, $runtimeProfileRoot | Out-Null

$candidateManifest = Get-Content -LiteralPath $candidateManifestFullPath -Raw | ConvertFrom-Json
$candidateId = [string](Get-ObjectPropertyValue -Object $candidateManifest -Name 'candidate_id' -DefaultValue '')
$candidateKind = [string](Get-ObjectPropertyValue -Object $candidateManifest -Name 'candidate_kind' -DefaultValue '')
$source = Get-ObjectPropertyValue -Object $candidateManifest -Name 'source'
$windows = Get-ObjectPropertyValue -Object $candidateManifest -Name 'windows'
$sourceRevision = ([string](Get-ObjectPropertyValue -Object $source -Name 'git_sha' -DefaultValue '')).ToLowerInvariant()
$sourceClean = Get-ObjectPropertyValue -Object $source -Name 'clean'
$expectedExecutableSha256 = ([string](Get-ObjectPropertyValue -Object $windows -Name 'executable_sha256' -DefaultValue '')).ToLowerInvariant()
$stagedExecutablePath = [string](Get-ObjectPropertyValue -Object $windows -Name 'executable_path' -DefaultValue '')
if ([int](Get-ObjectPropertyValue -Object $candidateManifest -Name 'schema_version' -DefaultValue 0) -ne 1 -or
	$candidateId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$' -or $candidateKind -cne 'release' -or
	$sourceRevision -notmatch '^[0-9a-f]{40}$' -or $sourceClean -isnot [bool] -or -not [bool]$sourceClean -or
	$expectedExecutableSha256 -notmatch '^[0-9a-f]{64}$' -or [string]::IsNullOrWhiteSpace($stagedExecutablePath)) {
	throw 'Candidate manifest must be a clean schema-v1 release candidate.'
}
if (-not [IO.Path]::IsPathRooted($stagedExecutablePath)) {
	$stagedExecutablePath = Join-Path (Split-Path -Parent $candidateManifestFullPath) $stagedExecutablePath
}
$stagedExecutablePath = Get-RequiredFile -Path $stagedExecutablePath -Label 'Candidate manifest executable'
if ((Get-FileSha256 -Path $stagedExecutablePath) -cne $expectedExecutableSha256) {
	throw 'Candidate manifest executable hash does not match its executable_sha256.'
}

$msiPayloadEvidenceFile = $null
if (-not [string]::IsNullOrWhiteSpace($MsiPayloadEvidencePath)) {
	$msiPayloadEvidenceFile = [ordered]@{
		path = $providedMsiPayloadEvidenceFullPath
		sha256 = Get-FileSha256 -Path $providedMsiPayloadEvidenceFullPath
	}
	$msiPayloadEvidence = Get-Content -LiteralPath $providedMsiPayloadEvidenceFullPath -Raw | ConvertFrom-Json
} else {
	$msiPayloadEvidence = Get-WindowsMsiPayloadVerification -CandidateClientMsi $candidateMsiPath `
		-CandidateExecutable $stagedExecutablePath -WorkingRoot $workRoot
}
Assert-WindowsMsiPayloadEvidence -Evidence $msiPayloadEvidence -CandidateClientMsi $candidateMsiPath `
	-CandidateExecutable $stagedExecutablePath | Out-Null

$previousMetadata = Get-MsiMetadata -Path $previousMsiPath
$candidateMetadata = Get-MsiMetadata -Path $candidateMsiPath
if ($previousMetadata.product_code -ceq $candidateMetadata.product_code) {
	throw 'Previous and candidate client MSIs must have distinct ProductCode values.'
}
if ($previousMetadata.upgrade_code -cne $candidateMetadata.upgrade_code) {
	throw 'Previous and candidate client MSIs must share one UpgradeCode.'
}
if (Test-MsiProductInstalled -ProductCode $previousMetadata.product_code) {
	throw 'PreviousClientMsi is already installed; use a clean disposable runner.'
}
if (Test-MsiProductInstalled -ProductCode $candidateMetadata.product_code) {
	throw 'CandidateClientMsi is already installed; use a clean disposable runner.'
}
if (Test-Path -LiteralPath $installedExecutableFullPath) {
	throw "InstalledExecutablePath already exists; use a clean disposable runner: $installedExecutableFullPath"
}
$preflightProductsAbsent = $true

$profileBefore = Get-ProfileSnapshot -Profile $profile
$snapshots = [ordered]@{ before = $profileBefore }
$operations = [ordered]@{}
$cleanupAttempts = [Collections.Generic.List[object]]::new()
$runtimeProcess = $null
$savedAutomationEnvironment = [ordered]@{
	port = $env:MUMBLE_MODERN_AUTOMATION_PORT
	token = $env:MUMBLE_MODERN_AUTOMATION_TOKEN
	offscreen = $env:MUMBLE_MODERN_AUTOMATION_OFFSCREEN
}
$runtimeReadyState = $null
$failureMessage = $null
$verification = [ordered]@{
	candidate_msi_payload_hash_match = $true
	previous_install_verified = $false
	profile_seed_unchanged_after_previous_install = $false
	candidate_upgrade_verified = $false
	installed_executable_hash_match = $false
	profile_seed_unchanged_after_candidate_upgrade = $false
	runtime_ready = $false
	profile_seed_unchanged_after_runtime = $false
	candidate_uninstalled = $false
	installed_executable_removed = $false
	profile_seed_unchanged_after_uninstall = $false
}

try {
	$operations.previous_install = Invoke-MsiOperation -Operation install -Target $previousMsiPath `
		-LogPath (Join-Path $logRoot 'previous-install.log')
	$verification.previous_install_verified = (Test-MsiProductInstalled -ProductCode $previousMetadata.product_code) -and
		(Test-Path -LiteralPath $installedExecutableFullPath -PathType Leaf)
	if (-not $verification.previous_install_verified) { throw 'Previous MSI installation did not register the product and executable.' }
	$snapshots.after_previous_install = Get-ProfileSnapshot -Profile $profile
	$verification.profile_seed_unchanged_after_previous_install = Test-ProfileSnapshotEqual -Before $profileBefore -After $snapshots.after_previous_install
	if (-not $verification.profile_seed_unchanged_after_previous_install) { throw 'Previous MSI installation modified the isolated profile seed.' }

	$operations.candidate_upgrade = Invoke-MsiOperation -Operation install -Target $candidateMsiPath `
		-LogPath (Join-Path $logRoot 'candidate-upgrade.log')
	$verification.candidate_upgrade_verified = (Test-MsiProductInstalled -ProductCode $candidateMetadata.product_code) -and
		-not (Test-MsiProductInstalled -ProductCode $previousMetadata.product_code)
	if (-not $verification.candidate_upgrade_verified) { throw 'Candidate MSI did not replace the previous product registration.' }
	if (-not (Test-Path -LiteralPath $installedExecutableFullPath -PathType Leaf)) { throw 'Candidate upgrade did not install mumble.exe at InstalledExecutablePath.' }
	$installedExecutableSha256 = Get-FileSha256 -Path $installedExecutableFullPath
	$verification.installed_executable_hash_match = $installedExecutableSha256 -ceq $expectedExecutableSha256
	if (-not $verification.installed_executable_hash_match) { throw 'Installed mumble.exe SHA-256 does not match the exact release candidate.' }
	$snapshots.after_candidate_upgrade = Get-ProfileSnapshot -Profile $profile
	$verification.profile_seed_unchanged_after_candidate_upgrade = Test-ProfileSnapshotEqual -Before $profileBefore -After $snapshots.after_candidate_upgrade
	if (-not $verification.profile_seed_unchanged_after_candidate_upgrade) { throw 'Candidate MSI upgrade modified the isolated profile seed.' }

	$runtimeConfigPath = Join-Path $runtimeProfileRoot ([IO.Path]::GetFileName([string]$profile.config))
	$runtimeDatabasePath = Join-Path $runtimeProfileRoot 'mumble.sqlite'
	Copy-Item -LiteralPath $profile.config -Destination $runtimeConfigPath
	Copy-Item -LiteralPath $profile.database -Destination $runtimeDatabasePath
	$runtimePort = $AutomationPort -gt 0 ? $AutomationPort : (Get-FreeTcpPort)
	$runtimeToken = [Guid]::NewGuid().ToString('N')
	$env:MUMBLE_MODERN_AUTOMATION_PORT = [string]$runtimePort
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $runtimeToken
	$env:MUMBLE_MODERN_AUTOMATION_OFFSCREEN = '1'
	$runtimeStartedAt = [DateTime]::UtcNow
	$runtimeProcess = Start-Process -FilePath $installedExecutableFullPath `
		-ArgumentList @('--multiple', '--config', ('"{0}"' -f $runtimeConfigPath)) -PassThru
	$runtimeReadyState = Wait-QmlRuntimeReady -Port $runtimePort -Token $runtimeToken -Process $runtimeProcess `
		-TimeoutSeconds $StartupTimeoutSeconds
	$verification.runtime_ready = $true
	$operations.runtime_start = [ordered]@{
		process_id = $runtimeProcess.Id
		started_at_utc = $runtimeStartedAt.ToString('o')
		ready_at_utc = [DateTime]::UtcNow.ToString('o')
		automation_port = $runtimePort
		frontend = [string]$runtimeReadyState.frontend
		window_ready = [bool]$runtimeReadyState.windowReady
		main_capture_ready = [bool]$runtimeReadyState.mainCaptureReady
	}
	Stop-Process -Id $runtimeProcess.Id -Force -ErrorAction Stop
	$runtimeProcess.WaitForExit(10000) | Out-Null
	$runtimeProcess = $null
	$snapshots.after_runtime = Get-ProfileSnapshot -Profile $profile
	$verification.profile_seed_unchanged_after_runtime = Test-ProfileSnapshotEqual -Before $profileBefore -After $snapshots.after_runtime
	if (-not $verification.profile_seed_unchanged_after_runtime) { throw 'Runtime verification modified the source profile seed.' }

	$operations.candidate_uninstall = Invoke-MsiOperation -Operation uninstall -Target $candidateMetadata.product_code `
		-LogPath (Join-Path $logRoot 'candidate-uninstall.log')
	$verification.candidate_uninstalled = -not (Test-MsiProductInstalled -ProductCode $candidateMetadata.product_code)
	$verification.installed_executable_removed = -not (Test-Path -LiteralPath $installedExecutableFullPath)
	if (-not $verification.candidate_uninstalled -or -not $verification.installed_executable_removed) {
		throw 'Candidate uninstall did not remove the product registration and installed mumble.exe.'
	}
	$snapshots.after_uninstall = Get-ProfileSnapshot -Profile $profile
	$verification.profile_seed_unchanged_after_uninstall = Test-ProfileSnapshotEqual -Before $profileBefore -After $snapshots.after_uninstall
	if (-not $verification.profile_seed_unchanged_after_uninstall) { throw 'Candidate uninstall modified the isolated profile seed.' }
} catch {
	$failureMessage = $_.Exception.Message
} finally {
	if ($null -ne $runtimeProcess) {
		try { Stop-Process -Id $runtimeProcess.Id -Force -ErrorAction Stop } catch { }
	}
	$env:MUMBLE_MODERN_AUTOMATION_PORT = $savedAutomationEnvironment.port
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $savedAutomationEnvironment.token
	$env:MUMBLE_MODERN_AUTOMATION_OFFSCREEN = $savedAutomationEnvironment.offscreen

	if ($preflightProductsAbsent) {
		foreach ($metadata in @($candidateMetadata, $previousMetadata)) {
			try {
				if (Test-MsiProductInstalled -ProductCode $metadata.product_code) {
					$cleanup = Invoke-MsiOperation -Operation uninstall -Target $metadata.product_code `
						-LogPath (Join-Path $logRoot ("cleanup-{0}.log" -f $metadata.product_code.Trim('{}'))) -CleanupAttempt
					$cleanup['product_code'] = $metadata.product_code
					$cleanup['removed'] = -not (Test-MsiProductInstalled -ProductCode $metadata.product_code)
					$cleanupAttempts.Add($cleanup)
					if (-not $cleanup.removed -and [string]::IsNullOrWhiteSpace($failureMessage)) {
						$failureMessage = "Cleanup could not remove MSI product $($metadata.product_code)."
					}
				}
			} catch {
				$cleanupAttempts.Add([ordered]@{ product_code = $metadata.product_code; removed = $false; error = $_.Exception.Message })
				if ([string]::IsNullOrWhiteSpace($failureMessage)) { $failureMessage = "Installer cleanup failed: $($_.Exception.Message)" }
			}
		}
	}
}

$allVerificationPassed = @($verification.Values | Where-Object { $_ -isnot [bool] -or -not [bool]$_ }).Count -eq 0
$eligible = [string]::IsNullOrWhiteSpace($failureMessage) -and $allVerificationPassed
$evidence = [ordered]@{
	schema_version = 1
	artifact_kind = 'windows_installer_upgrade_evidence'
	gate_id = $gateId
	status = $eligible ? 'passed' : 'failed'
	eligible = $eligible
	completed_at_utc = [DateTime]::UtcNow.ToString('o')
	candidate = [ordered]@{
		id = $candidateId
		source_revision = $sourceRevision
		executable_sha256 = $expectedExecutableSha256
	}
	inputs = [ordered]@{
		candidate_manifest = [ordered]@{ path = $candidateManifestFullPath; sha256 = Get-FileSha256 -Path $candidateManifestFullPath }
		candidate_msi_payload = [ordered]@{
			evidence_file = $msiPayloadEvidenceFile
			candidate_client_msi_sha256 = [string]$msiPayloadEvidence.candidate_client_msi.sha256
			candidate_executable_sha256 = [string]$msiPayloadEvidence.candidate_executable.sha256
			embedded_executable_sha256 = [string]$msiPayloadEvidence.embedded_payload.sha256
			exact_executable_sha256_match = [bool]$msiPayloadEvidence.verification.exact_executable_sha256_match
		}
		previous_client_msi = $previousMetadata
		candidate_client_msi = $candidateMetadata
		installed_executable_path = $installedExecutableFullPath
		profile_seed = [ordered]@{
			mode = $profile.mode
			manifest_sha256 = $profileBefore.manifest_sha256
			file_count = $profileBefore.file_count
		}
	}
	operations = $operations
	verification = $verification
	profile_snapshots = $snapshots
	cleanup_attempts = @($cleanupAttempts)
	error = [string]::IsNullOrWhiteSpace($failureMessage) ? $null : $failureMessage
}
Write-InstallerEvidenceJson -Document $evidence
$evidence | ConvertTo-Json -Depth 30
if (-not $eligible) {
	throw "Windows installer upgrade verification failed closed: $failureMessage. Evidence: $outputFullPath"
}
