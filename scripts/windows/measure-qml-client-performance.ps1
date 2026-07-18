[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Executable,

	[Parameter(Mandatory = $true)]
	[string]$ConfigPath,

	[int]$Runs = 5,
	[int]$StartupTimeoutSeconds = 20,
	[int]$WorkloadReadyTimeoutSeconds = 30,
	[int]$IdleSeconds = 5,
	[int]$RoomSwitchIterations = 40,
	[int]$TalkStateTransitions = 40,
	[int]$MinimumFrameSamples = 30,
	[int]$MinimumInputSamples = 40,
	[int]$AutomationPort = 0,
	[string]$AutomationToken = "",
	[string]$OutputPath = ".tmp\qml-performance.json",
	[string]$WebBaselinePath = "",
	[string]$ChatPerfTracePath = "",
	[string]$CandidateId = "",
	[string]$SourceCommit = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($Runs -ne 5) { throw "Runs must be exactly five for the locked Windows performance contract." }
if ($RoomSwitchIterations -lt 40) { throw "RoomSwitchIterations must be at least 40." }
if ($TalkStateTransitions -lt 40) { throw "TalkStateTransitions must be at least 40." }
if ($MinimumFrameSamples -lt 1) { throw "MinimumFrameSamples must be at least one." }
if ($MinimumInputSamples -lt 40) { throw "MinimumInputSamples must be at least 40." }
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$configFilePath = (Resolve-Path -LiteralPath $ConfigPath).Path
$executableSha256 = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$resolvedSourceCommit = $SourceCommit.Trim().ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($resolvedSourceCommit)) {
	try {
		$resolvedSourceCommit = ([string](& git -C $repoRoot rev-parse HEAD 2>$null)).Trim().ToLowerInvariant()
		if ($LASTEXITCODE -ne 0) { $resolvedSourceCommit = "" }
	} catch {
		$resolvedSourceCommit = ""
	}
}
if (-not [string]::IsNullOrWhiteSpace($resolvedSourceCommit) -and
	$resolvedSourceCommit -notmatch '^[0-9a-f]{40,64}$') {
	throw "SourceCommit must be a 40- or 64-character hexadecimal Git object ID."
}
$resolvedCandidateId = $CandidateId.Trim()
if ([string]::IsNullOrWhiteSpace($resolvedCandidateId)) {
	$sourceToken = if ([string]::IsNullOrWhiteSpace($resolvedSourceCommit)) {
		"unknown-source"
	} else {
		$resolvedSourceCommit.Substring(0, [Math]::Min(12, $resolvedSourceCommit.Length))
	}
	$resolvedCandidateId = "windows-qml-$sourceToken-$($executableSha256.Substring(0, 12))"
}
if ($resolvedCandidateId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
	throw "CandidateId must start with a letter or number and contain at most 128 letters, numbers, '.', '_' or '-' characters."
}
$outputFile = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFile
$performanceContractId = "windows-qml-performance-v2"
$performanceSchemaVersion = 2
$requiredModelResetCounters = @("room", "participant", "navigation", "chat", "action", "operation")
$requiredChatScrollInputSamples = 40

if (-not ("MumbleQmlPerformanceWindow" -as [type])) {
	Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MumbleQmlPerformanceWindow {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);
}
"@
}

function Set-QmlPerformanceWindowTopmost {
	param(
		[Parameter(Mandatory = $true)][Diagnostics.Process]$Process,
		[Parameter(Mandatory = $true)][bool]$Topmost
	)
	$Process.Refresh()
	$window = $Process.MainWindowHandle
	if ($window -eq [IntPtr]::Zero) { throw "The QML benchmark process has no top-level window handle." }
	# A visible but occluded D3D/Qt Quick window is presentation-throttled by
	# Windows. Restore it and keep it above ordinary windows for the measured
	# phases so frameSwapped represents product rendering rather than occlusion.
	[void][MumbleQmlPerformanceWindow]::ShowWindowAsync($window, 9) # SW_RESTORE
	$insertAfter = if ($Topmost) { [IntPtr](-1) } else { [IntPtr](-2) }
	$flags = [uint32](0x0001 -bor 0x0002 -bor 0x0010 -bor 0x0040) # NOSIZE|NOMOVE|NOACTIVATE|SHOWWINDOW
	if (-not [MumbleQmlPerformanceWindow]::SetWindowPos($window, $insertAfter, 0, 0, 0, 0, $flags)) {
		throw "Unable to set the QML benchmark window presentation state (Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
	}
}
$syncUiOperationDetectionScope = [ordered]@{
	classification = "bounded-hot-path-contract"
	dynamic_counter_categories = @("network", "plugin", "file")
	dynamic_counter_external_call_sites = 0
	dynamic_counter_semantics = "Explicit QmlPerformanceMonitor reports are counted during connected-idle, room-switch, chat-scroll, and talk-state sampling. Missing or malformed counters fail closed."
	static_contract_invariants = @(
		"connect/performance trace appenders enqueue writes instead of opening files on the calling path",
		"UserState local preference reads are delegated to a bounded QtConcurrent database worker",
		"ServerSync shortcuts and ChannelState filter reads are coalesced on one bounded database worker",
		"room and participant/message render-state builders contain no direct QFile, QSaveFile, or database blob reads",
		"MainWindow comment and ACL blob reads use a bounded cache-first worker with guarded continuations",
		"avatar cache misses enqueue worker hydration before any Database access",
		"positional plugin ABI fetches execute only after the shared plugin worker enqueue"
	)
	limitation = "This revision has no external recordSyncUiOperationViolation call sites and does not provide whole-process interception of arbitrary Qt, OS, or third-party synchronous I/O. Runtime zero counts are bounded evidence and must be read together with the static hot-path invariants."
}

function Get-Percentile {
	param([double[]]$Values, [double]$Percentile)
	if ($Values.Count -eq 0) { return $null }
	$sorted = @($Values | Sort-Object)
	$index = [Math]::Ceiling(($Percentile / 100.0) * $sorted.Count) - 1
	return [double]$sorted[[Math]::Max(0, [Math]::Min($sorted.Count - 1, $index))]
}

function Get-ByteArraySha256 {
	param([Parameter(Mandatory = $true)][byte[]]$Bytes)
	$sha256 = [Security.Cryptography.SHA256]::Create()
	try {
		return ([BitConverter]::ToString($sha256.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
	} finally { $sha256.Dispose() }
}

function Get-FrozenProfileSeed {
	param([Parameter(Mandatory = $true)][string]$SourceConfigPath)
	$configBytes = [IO.File]::ReadAllBytes($SourceConfigPath)
	$configHash = Get-ByteArraySha256 -Bytes $configBytes
	$currentConfigHash = (Get-FileHash -LiteralPath $SourceConfigPath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($configHash -ne $currentConfigHash) {
		throw "The source config changed while the frozen profile snapshot was being captured."
	}
	$configText = [Text.Encoding]::UTF8.GetString($configBytes).TrimStart([char]0xFEFF)
	$config = $configText | ConvertFrom-Json
	if ($null -eq $config -or -not ($config.PSObject.Properties.Name -contains "misc") -or $null -eq $config.misc) {
		throw "The source config must expose misc.database_location for frozen-profile isolation."
	}
	$databaseProperty = $config.misc.PSObject.Properties["database_location"]
	if ($null -eq $databaseProperty -or [string]::IsNullOrWhiteSpace([string]$databaseProperty.Value)) {
		throw "The source config must expose a non-empty misc.database_location for frozen-profile isolation."
	}
	$databaseLocation = [string]$databaseProperty.Value
	$databasePath = if ([IO.Path]::IsPathRooted($databaseLocation)) {
		[IO.Path]::GetFullPath($databaseLocation)
	} else {
		[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $SourceConfigPath) $databaseLocation))
	}
	if (-not (Test-Path -LiteralPath $databasePath -PathType Leaf)) {
		throw "The frozen-profile source database does not exist: $databasePath"
	}
	$databaseCompanions = @("$databasePath-wal", "$databasePath-shm", "$databasePath-journal")
	$activeCompanions = @($databaseCompanions | Where-Object { Test-Path -LiteralPath $_ })
	if ($activeCompanions.Count -gt 0) {
		throw "The source SQLite database has active WAL/journal companions and cannot be frozen consistently: $($activeCompanions -join ', ')"
	}
	$databaseBytes = [IO.File]::ReadAllBytes($databasePath)
	$databaseHash = Get-ByteArraySha256 -Bytes $databaseBytes
	$currentDatabaseHash = (Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($databaseHash -ne $currentDatabaseHash) {
		throw "The source database changed while the frozen profile snapshot was being captured."
	}
	$seedMaterial = [Text.Encoding]::UTF8.GetBytes("$configHash|$databaseHash")
	return [pscustomobject]@{
		config_text = $configText
		database_bytes = $databaseBytes
		profile_seed_sha256 = Get-ByteArraySha256 -Bytes $seedMaterial
		source_config_path = $SourceConfigPath
		source_config_sha256 = $configHash
		source_database_path = $databasePath
		source_database_sha256 = $databaseHash
		source_database_length_bytes = $databaseBytes.LongLength
		database_companion_paths = $databaseCompanions
	}
}

function New-FrozenRunProfile {
	param(
		[Parameter(Mandatory = $true)]$Seed,
		[Parameter(Mandatory = $true)][string]$RunDirectory,
		[Parameter(Mandatory = $true)][int]$Run
	)
	if (Test-Path -LiteralPath $RunDirectory) {
		throw "The isolated run directory already exists: $RunDirectory"
	}
	New-Item -ItemType Directory -Path $RunDirectory | Out-Null
	$databasePath = Join-Path $RunDirectory "mumble.sqlite"
	[IO.File]::WriteAllBytes($databasePath, [byte[]]$Seed.database_bytes)
	$databaseSeedHash = (Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($databaseSeedHash -ne [string]$Seed.source_database_sha256) {
		throw "Run $Run database clone did not match the frozen source snapshot."
	}
	$config = ([string]$Seed.config_text) | ConvertFrom-Json
	$config.misc.database_location = ($databasePath -replace "\\", "/")
	$configPath = Join-Path $RunDirectory "mumble_settings.json"
	$configJson = $config | ConvertTo-Json -Depth 100
	[IO.File]::WriteAllText($configPath, $configJson, [Text.UTF8Encoding]::new($false))
	$configSeedHash = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
	return [ordered]@{
		run = $Run
		profile_seed_sha256 = [string]$Seed.profile_seed_sha256
		config_path = $configPath
		database_path = $databasePath
		config_seed_sha256 = $configSeedHash
		database_seed_sha256 = $databaseSeedHash
		post_run_config_sha256 = $null
		post_run_database_sha256 = $null
		config_mutated_during_run = $null
		database_mutated_during_run = $null
		file_quiescence = $null
	}
}

function Test-FrozenProfileSourceUnchanged {
	param([Parameter(Mandatory = $true)]$Seed)
	try {
		$configHash = (Get-FileHash -LiteralPath $Seed.source_config_path -Algorithm SHA256).Hash.ToLowerInvariant()
		$databaseHash = (Get-FileHash -LiteralPath $Seed.source_database_path -Algorithm SHA256).Hash.ToLowerInvariant()
		$activeCompanions = @($Seed.database_companion_paths | Where-Object { Test-Path -LiteralPath $_ })
		$unchanged = $configHash -eq [string]$Seed.source_config_sha256 -and
			$databaseHash -eq [string]$Seed.source_database_sha256 -and $activeCompanions.Count -eq 0
		return [pscustomobject]@{
			unchanged = $unchanged
			reason = if ($unchanged) { $null } else { "source config/database hashes changed or an SQLite companion appeared" }
			config_sha256_after = $configHash
			database_sha256_after = $databaseHash
			active_database_companions = $activeCompanions
		}
	} catch {
		return [pscustomobject]@{
			unchanged = $false
			reason = $_.Exception.Message
			config_sha256_after = $null
			database_sha256_after = $null
			active_database_companions = @()
		}
	}
}

function Get-ChatPerfTraceAnalysis {
	param([AllowEmptyCollection()][string[]]$TraceLines)

	$maxDurations = @($TraceLines | ForEach-Object {
		if ($_ -match '\bmax_ms=(?<duration>[0-9.]+)') { [double]$Matches.duration }
	})
	$legacySnapshotLines = @($TraceLines | Where-Object { $_ -match '(?i)full[._ -]?snapshot|buildModernShellSnapshot' })
	$steadyStateBootstrapLines = @($TraceLines | Where-Object {
		$_ -match '(?i)^\[chat-perf\]\[value\]\s+qml\.full_bootstrap\.steady_state_violation\b'
	})
	$steadyStateBootstrapTotal = 0L
	foreach ($line in $steadyStateBootstrapLines) {
		if ($line -match '\btotal=(?<count>[0-9]+)\b') {
			$steadyStateBootstrapTotal += [int64]$Matches.count
		} else {
			# A matching counter line without a parseable total must never make the
			# correctness gate pass silently.
			++$steadyStateBootstrapTotal
		}
	}
	$modelResetCounts = [ordered]@{}
	foreach ($counter in @("room", "participant", "navigation", "chat", "action", "operation")) {
		$modelResetCounts[$counter] = 0L
	}
	$modelResetLineCount = 0
	$modelResetTotal = 0L
	foreach ($line in $TraceLines) {
		if ($line -notmatch '(?i)^\[chat-perf\]\[value\]\s+qml\.(?<counter>rooms?|participants?|navigation|chat|actions?|operations?)\.model_reset\b') {
			continue
		}
		++$modelResetLineCount
		$counter = $Matches.counter.ToLowerInvariant()
		if ($counter -eq "rooms") { $counter = "room" }
		if ($counter -eq "participants") { $counter = "participant" }
		if ($counter -eq "actions") { $counter = "action" }
		if ($counter -eq "operations") { $counter = "operation" }
		$value = 1L
		if ($line -match '\btotal=(?<count>[0-9]+)\b') {
			$value = [int64]$Matches.count
		}
		$modelResetCounts[$counter] += $value
		$modelResetTotal += $value
	}

	return [pscustomobject]@{
		max_observed_timing_ms = if ($maxDurations.Count -gt 0) { ($maxDurations | Measure-Object -Maximum).Maximum } else { $null }
		legacy_full_snapshot_line_count = $legacySnapshotLines.Count
		steady_state_full_bootstrap_line_count = $steadyStateBootstrapLines.Count
		steady_state_full_bootstrap_total = $steadyStateBootstrapTotal
		model_reset_line_count = $modelResetLineCount
		model_reset_total = $modelResetTotal
		model_reset_counts = $modelResetCounts
	}
}

function Get-FreeTcpPort {
	$listener = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
}

function Get-WindowsReferenceMachineFingerprint {
	$machineGuid = ""
	$systemUuid = ""
	try {
		$machineGuid = [string](Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Cryptography' -Name MachineGuid -ErrorAction Stop)
	} catch { }
	try {
		$systemUuid = [string](Get-CimInstance Win32_ComputerSystemProduct -ErrorAction Stop | Select-Object -First 1 -ExpandProperty UUID)
	} catch { }
	if ([string]::IsNullOrWhiteSpace($machineGuid) -or [string]::IsNullOrWhiteSpace($systemUuid)) {
		throw "Unable to establish the Windows reference-machine fingerprint."
	}
	$sha256 = [Security.Cryptography.SHA256]::Create()
	try {
		$bytes = [Text.Encoding]::UTF8.GetBytes("$machineGuid|$systemUuid")
		return ([BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
	} finally { $sha256.Dispose() }
}

function Get-ValidatedWebPerformanceBaseline {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$MachineFingerprint,
		[Parameter(Mandatory = $true)]$FrozenProfileSeed,
		[Parameter(Mandatory = $true)][int]$ExpectedSchemaVersion,
		[Parameter(Mandatory = $true)][string]$ExpectedContractId
	)

	if ([string]::IsNullOrWhiteSpace($Path)) {
		throw "WebBaselinePath is required by the locked Windows performance contract."
	}
	$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
	$baselineBytes = [IO.File]::ReadAllBytes($resolvedPath)
	$baselineFileHash = Get-ByteArraySha256 -Bytes $baselineBytes
	if ($baselineFileHash -cne (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash.ToLowerInvariant()) {
		throw "The Web baseline changed while its preflight snapshot was being captured."
	}
	$baselineText = [Text.Encoding]::UTF8.GetString($baselineBytes).TrimStart([char]0xFEFF)
	$baseline = $baselineText | ConvertFrom-Json
	$errors = [Collections.Generic.List[string]]::new()
	$schemaVersion = if ($baseline.PSObject.Properties.Name -contains "schema_version") { [int]$baseline.schema_version } else { 0 }
	$contractId = if ($baseline.PSObject.Properties.Name -contains "contract_id") { [string]$baseline.contract_id } else { "" }
	$kind = if ($baseline.PSObject.Properties.Name -contains "kind") { [string]$baseline.kind } else { "" }
	$frontend = if ($baseline.PSObject.Properties.Name -contains "frontend") { [string]$baseline.frontend } else { "" }
	$baselineMachine = if ($baseline.PSObject.Properties.Name -contains "machine_fingerprint_sha256") { [string]$baseline.machine_fingerprint_sha256 } else { "" }
	$rootProfileSeed = if ($baseline.PSObject.Properties.Name -contains "profile_seed_sha256") { [string]$baseline.profile_seed_sha256 } else { "" }
	$baselineSummary = if ($baseline.PSObject.Properties.Name -contains "summary") { $baseline.summary } else { $null }
	$baselineProvenance = if ($baseline.PSObject.Properties.Name -contains "frozen_profile_provenance") {
		$baseline.frozen_profile_provenance
	} else {
		$null
	}
	$baselineStartupMedian = $null
	$baselineIdleWorkingSetMedian = $null
	$rendererConfirmedRuns = 0
	$baselineProfileSeed = ""
	$baselineConfigHash = ""
	$baselineDatabaseHash = ""

	if ($schemaVersion -ne $ExpectedSchemaVersion) { $errors.Add("schema_version must be $ExpectedSchemaVersion") }
	if ($contractId -cne $ExpectedContractId) { $errors.Add("contract_id must be '$ExpectedContractId'") }
	if ($kind -cne "webengine_reference_baseline") { $errors.Add("kind must be 'webengine_reference_baseline'") }
	if ($frontend -cne "web-reference") { $errors.Add("frontend must be 'web-reference'") }
	if ($baselineMachine -cne $MachineFingerprint) { $errors.Add("machine_fingerprint_sha256 does not match this reference machine") }
	if ($rootProfileSeed -cne [string]$FrozenProfileSeed.profile_seed_sha256) {
		$errors.Add("profile_seed_sha256 does not match the candidate's frozen config/database snapshot")
	}

	if ($null -eq $baselineSummary) {
		$errors.Add("summary is missing")
	} else {
		if (-not ($baselineSummary.PSObject.Properties.Name -contains "runs") -or [int]$baselineSummary.runs -ne 5) {
			$errors.Add("summary.runs must be exactly five")
		}
		if (-not ($baselineSummary.PSObject.Properties.Name -contains "chromium_renderer_confirmed_runs")) {
			$errors.Add("summary.chromium_renderer_confirmed_runs is missing")
		} else {
			$rendererConfirmedRuns = [int]$baselineSummary.chromium_renderer_confirmed_runs
			if ($rendererConfirmedRuns -ne 5) {
				$errors.Add("summary.chromium_renderer_confirmed_runs must be exactly five")
			}
		}
		if (-not ($baselineSummary.PSObject.Properties.Name -contains "startup_to_interactive_median_ms")) {
			$errors.Add("summary.startup_to_interactive_median_ms is missing")
		} else {
			try { $baselineStartupMedian = [double]$baselineSummary.startup_to_interactive_median_ms } catch { }
			if ($null -eq $baselineStartupMedian -or [double]::IsNaN($baselineStartupMedian) -or
				[double]::IsInfinity($baselineStartupMedian) -or $baselineStartupMedian -le 0) {
				$errors.Add("summary.startup_to_interactive_median_ms must be a positive finite number")
			}
		}
		if (-not ($baselineSummary.PSObject.Properties.Name -contains "connected_idle_working_set_median_bytes")) {
			$errors.Add("summary.connected_idle_working_set_median_bytes is missing")
		} else {
			try { $baselineIdleWorkingSetMedian = [double]$baselineSummary.connected_idle_working_set_median_bytes } catch { }
			if ($null -eq $baselineIdleWorkingSetMedian -or [double]::IsNaN($baselineIdleWorkingSetMedian) -or
				[double]::IsInfinity($baselineIdleWorkingSetMedian) -or $baselineIdleWorkingSetMedian -le 0) {
				$errors.Add("summary.connected_idle_working_set_median_bytes must be a positive finite number")
			}
		}
	}

	if ($null -eq $baselineProvenance) {
		$errors.Add("frozen_profile_provenance is missing")
	} else {
		$baselineProfileSeed = if ($baselineProvenance.PSObject.Properties.Name -contains "profile_seed_sha256") {
			[string]$baselineProvenance.profile_seed_sha256
		} else { "" }
		$baselineConfigHash = if ($baselineProvenance.PSObject.Properties.Name -contains "source_config_sha256") {
			[string]$baselineProvenance.source_config_sha256
		} else { "" }
		$baselineDatabaseHash = if ($baselineProvenance.PSObject.Properties.Name -contains "source_database_sha256") {
			[string]$baselineProvenance.source_database_sha256
		} else { "" }
		if ($baselineProfileSeed -cne [string]$FrozenProfileSeed.profile_seed_sha256) {
			$errors.Add("frozen_profile_provenance.profile_seed_sha256 does not match the candidate's frozen config/database snapshot")
		}
		if ($baselineConfigHash -cne [string]$FrozenProfileSeed.source_config_sha256) {
			$errors.Add("frozen_profile_provenance.source_config_sha256 does not match the candidate config snapshot")
		}
		if ($baselineDatabaseHash -cne [string]$FrozenProfileSeed.source_database_sha256) {
			$errors.Add("frozen_profile_provenance.source_database_sha256 does not match the candidate SQLite seed")
		}
	}

	$legacySummary = if ($null -ne $baselineSummary) { $baselineSummary } else { $baseline }
	$diagnostics = [ordered]@{
		path = $resolvedPath
		file_sha256 = $baselineFileHash
		schema_version = $schemaVersion
		contract_id = $contractId
		kind = $kind
		frontend = $frontend
		machine_fingerprint_sha256 = $baselineMachine
		profile_seed_sha256 = $rootProfileSeed
		chromium_renderer_confirmed_runs = $rendererConfirmedRuns
		frozen_profile_provenance = [ordered]@{
			profile_seed_sha256 = $baselineProfileSeed
			source_config_sha256 = $baselineConfigHash
			source_database_sha256 = $baselineDatabaseHash
			matches_candidate = $errors.Count -eq 0
		}
		startup_to_interactive_median_ms = $baselineStartupMedian
		connected_idle_working_set_median_bytes = $baselineIdleWorkingSetMedian
		validation_errors = @($errors)
		legacy_startup_to_window_median_ms = if ($legacySummary.PSObject.Properties.Name -contains "startup_to_window_median_ms") { $legacySummary.startup_to_window_median_ms } else { $null }
		legacy_idle_working_set_median_bytes = if ($legacySummary.PSObject.Properties.Name -contains "idle_working_set_median_bytes") { $legacySummary.idle_working_set_median_bytes } else { $null }
	}
	if ($errors.Count -gt 0) {
		throw "Web baseline preflight failed before client startup: $($errors -join '; ')"
	}
	return [pscustomobject]@{
		document = $baseline
		summary = $baselineSummary
		startup_median_ms = [double]$baselineStartupMedian
		idle_working_set_median_bytes = [double]$baselineIdleWorkingSetMedian
		diagnostics = $diagnostics
	}
}

function Get-ProcessTreeIds {
	param([int]$RootProcessId)
	$processes = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name)
	$ids = [System.Collections.Generic.HashSet[int]]::new()
	$null = $ids.Add($RootProcessId)
	do {
		$added = $false
		foreach ($process in $processes) {
			if ($ids.Contains([int]$process.ParentProcessId) -and $ids.Add([int]$process.ProcessId)) { $added = $true }
		}
	} while ($added)
	return @($processes | Where-Object { $ids.Contains([int]$_.ProcessId) })
}

function Get-ProcessTreeMetrics {
	param([int]$RootProcessId)
	$tree = @(Get-ProcessTreeIds -RootProcessId $RootProcessId)
	$rows = @()
	$workingSetBytes = 0L
	foreach ($entry in $tree) {
		$live = Get-Process -Id ([int]$entry.ProcessId) -ErrorAction SilentlyContinue
		$workingSet = if ($live) { [int64]$live.WorkingSet64 } else { 0L }
		$workingSetBytes += $workingSet
		$rows += [pscustomobject][ordered]@{
			pid = [int]$entry.ProcessId
			parent_pid = [int]$entry.ParentProcessId
			name = [string]$entry.Name
			working_set_bytes = $workingSet
		}
	}
	$nameCounts = [ordered]@{}
	foreach ($group in @($rows | Group-Object -Property name | Sort-Object -Property Name)) {
		$nameCounts[[string]$group.Name] = $group.Count
	}
	return [pscustomobject]@{
		tree = $tree
		processes = $rows
		process_name_counts = $nameCounts
		process_count = $tree.Count
		working_set_bytes = $workingSetBytes
		chromium_process_count = @($tree | Where-Object { $_.Name -ieq "QtWebEngineProcess.exe" }).Count
	}
}

function Get-WebEngineModuleResidency {
	param([int]$RootProcessId)
	try {
		$process = Get-Process -Id $RootProcessId -ErrorAction Stop
		$moduleNames = @($process.Modules | ForEach-Object { [string]$_.ModuleName } | Sort-Object -Unique)
		$webEngineModules = @($moduleNames | Where-Object { $_ -match '(?i)webengine|chromium' })
		return [pscustomobject]@{
			measured = $true
			reason = $null
			webengine_module_count = $webEngineModules.Count
			webengine_modules = $webEngineModules
		}
	} catch {
		return [pscustomobject]@{
			measured = $false
			reason = $_.Exception.Message
			webengine_module_count = $null
			webengine_modules = @()
		}
	}
}

function Initialize-WindowsJobProcessStartTracker {
	if ("Mumble.WindowsPerformance.JobProcessStartTracker" -as [type]) { return }

	# The root process is created suspended, associated with an otherwise inactive
	# Job Object, and resumed only after the completion port and its reader thread
	# are armed. Job accounting's authoritative TotalProcesses counter is reconciled
	# against every NEW_PROCESS notification. Completion-port delivery alone is not
	# treated as proof: a missing or unresolved notification makes the gate fail.
	Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Mumble.WindowsPerformance {
    public sealed class JobProcessStartRecord {
        public int ProcessId { get; internal set; }
        public int ParentProcessId { get; internal set; }
        public string ProcessName { get; internal set; }
        public string ImagePath { get; internal set; }
        public string TimeCreated { get; internal set; }
        public bool MetadataResolved { get; internal set; }
        public int MetadataError { get; internal set; }
        public bool JobDescendant { get { return true; } }
    }

    public sealed class JobProcessStartSnapshot {
        public JobProcessStartRecord[] Records { get; internal set; }
        public UInt32 NotificationCount { get; internal set; }
        public UInt32 TotalProcesses { get; internal set; }
        public UInt32 ActiveProcesses { get; internal set; }
        public UInt32 UnresolvedProcessCount { get; internal set; }
        public bool AccountingReconciled { get; internal set; }
        public bool AllProcessesExited { get; internal set; }
        public Int64 TerminationWaitMilliseconds { get; internal set; }
        public bool TerminationTimedOut { get; internal set; }
        public bool Measured { get; internal set; }
        public string Reason { get; internal set; }
    }

    public sealed class JobProcessStartTracker : IDisposable {
        private const UInt32 CREATE_SUSPENDED = 0x00000004;
        private const UInt32 PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;
        private const UInt32 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
        private const UInt32 JOB_OBJECT_MSG_NEW_PROCESS = 6;
        private const int JobObjectBasicAccountingInformation = 1;
        private const int JobObjectAssociateCompletionPortInformation = 7;
        private const int JobObjectExtendedLimitInformation = 9;
        private static readonly IntPtr InvalidHandleValue = new IntPtr(-1);
        private static readonly UIntPtr JobCompletionKey = new UIntPtr(0x4d554d42u);
        private static readonly UIntPtr StopCompletionKey = new UIntPtr(0x53544f50u);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct STARTUPINFO {
            public UInt32 cb;
            public string lpReserved;
            public string lpDesktop;
            public string lpTitle;
            public UInt32 dwX;
            public UInt32 dwY;
            public UInt32 dwXSize;
            public UInt32 dwYSize;
            public UInt32 dwXCountChars;
            public UInt32 dwYCountChars;
            public UInt32 dwFillAttribute;
            public UInt32 dwFlags;
            public UInt16 wShowWindow;
            public UInt16 cbReserved2;
            public IntPtr lpReserved2;
            public IntPtr hStdInput;
            public IntPtr hStdOutput;
            public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_INFORMATION {
            public IntPtr hProcess;
            public IntPtr hThread;
            public UInt32 dwProcessId;
            public UInt32 dwThreadId;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JOBOBJECT_ASSOCIATE_COMPLETION_PORT {
            public IntPtr CompletionKey;
            public IntPtr CompletionPort;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
            public Int64 PerProcessUserTimeLimit;
            public Int64 PerJobUserTimeLimit;
            public UInt32 LimitFlags;
            public UIntPtr MinimumWorkingSetSize;
            public UIntPtr MaximumWorkingSetSize;
            public UInt32 ActiveProcessLimit;
            public UIntPtr Affinity;
            public UInt32 PriorityClass;
            public UInt32 SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IO_COUNTERS {
            public UInt64 ReadOperationCount;
            public UInt64 WriteOperationCount;
            public UInt64 OtherOperationCount;
            public UInt64 ReadTransferCount;
            public UInt64 WriteTransferCount;
            public UInt64 OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
            public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit;
            public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed;
            public UIntPtr PeakJobMemoryUsed;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION {
            public Int64 TotalUserTime;
            public Int64 TotalKernelTime;
            public Int64 ThisPeriodTotalUserTime;
            public Int64 ThisPeriodTotalKernelTime;
            public UInt32 TotalPageFaultCount;
            public UInt32 TotalProcesses;
            public UInt32 ActiveProcesses;
            public UInt32 TotalTerminatedProcesses;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_BASIC_INFORMATION {
            public IntPtr Reserved1;
            public IntPtr PebBaseAddress;
            public IntPtr Reserved2_0;
            public IntPtr Reserved2_1;
            public IntPtr UniqueProcessId;
            public IntPtr InheritedFromUniqueProcessId;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateProcessW(string applicationName, StringBuilder commandLine,
            IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles, UInt32 creationFlags,
            IntPtr environment, string currentDirectory, ref STARTUPINFO startupInfo,
            out PROCESS_INFORMATION processInformation);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateJobObject(IntPtr jobAttributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateIoCompletionPort(IntPtr fileHandle, IntPtr existingCompletionPort,
            UIntPtr completionKey, UInt32 numberOfConcurrentThreads);

        [DllImport("kernel32.dll", EntryPoint = "SetInformationJobObject", SetLastError = true)]
        private static extern bool SetCompletionPort(IntPtr job, int informationClass,
            ref JOBOBJECT_ASSOCIATE_COMPLETION_PORT information, UInt32 informationLength);

        [DllImport("kernel32.dll", EntryPoint = "SetInformationJobObject", SetLastError = true)]
        private static extern bool SetExtendedLimits(IntPtr job, int informationClass,
            ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION information, UInt32 informationLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool QueryInformationJobObject(IntPtr job, int informationClass,
            out JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information, UInt32 informationLength,
            out UInt32 returnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, UInt32 exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern UInt32 ResumeThread(IntPtr thread);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateProcess(IntPtr process, UInt32 exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetQueuedCompletionStatus(IntPtr completionPort, out UInt32 bytesTransferred,
            out UIntPtr completionKey, out IntPtr overlapped, UInt32 milliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool PostQueuedCompletionStatus(IntPtr completionPort, UInt32 bytesTransferred,
            UIntPtr completionKey, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(UInt32 desiredAccess, bool inheritHandle, UInt32 processId);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool QueryFullProcessImageName(IntPtr process, UInt32 flags,
            StringBuilder executableName, ref UInt32 size);

        [DllImport("ntdll.dll")]
        private static extern int NtQueryInformationProcess(IntPtr process, int processInformationClass,
            out PROCESS_BASIC_INFORMATION processInformation, int processInformationLength,
            out int returnLength);

        private readonly object sync = new object();
        private readonly List<JobProcessStartRecord> records = new List<JobProcessStartRecord>();
        private IntPtr job = IntPtr.Zero;
        private IntPtr completionPort = IntPtr.Zero;
        private Thread monitorThread;
        private string monitorError;
        private bool stopped;
        private JobProcessStartSnapshot finalSnapshot;

        public JobProcessStartTracker() {
            job = CreateJobObject(IntPtr.Zero, null);
            if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObject failed");
            try {
                var limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!SetExtendedLimits(job, JobObjectExtendedLimitInformation, ref limits,
                        (UInt32)Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION))))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "SetInformationJobObject limits failed");

                completionPort = CreateIoCompletionPort(InvalidHandleValue, IntPtr.Zero, UIntPtr.Zero, 1);
                if (completionPort == IntPtr.Zero)
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateIoCompletionPort failed");
                var association = new JOBOBJECT_ASSOCIATE_COMPLETION_PORT {
                    CompletionKey = new IntPtr(unchecked((long)JobCompletionKey.ToUInt64())),
                    CompletionPort = completionPort
                };
                if (!SetCompletionPort(job, JobObjectAssociateCompletionPortInformation, ref association,
                        (UInt32)Marshal.SizeOf(typeof(JOBOBJECT_ASSOCIATE_COMPLETION_PORT))))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Completion-port association failed");

                monitorThread = new Thread(Monitor) { IsBackground = true, Name = "Mumble performance process-start latch" };
                monitorThread.Start();
            } catch {
                DisposeNativeHandles();
                throw;
            }
        }

        public int StartSuspended(string executable, string[] arguments, string workingDirectory) {
            if (stopped) throw new InvalidOperationException("The process-start tracker is stopped.");
            var commandLine = new StringBuilder(QuoteArgument(executable));
            foreach (string argument in arguments ?? new string[0]) {
                commandLine.Append(' ').Append(QuoteArgument(argument ?? String.Empty));
            }
            var startup = new STARTUPINFO { cb = (UInt32)Marshal.SizeOf(typeof(STARTUPINFO)) };
            PROCESS_INFORMATION process;
            if (!CreateProcessW(executable, commandLine, IntPtr.Zero, IntPtr.Zero, false, CREATE_SUSPENDED,
                    IntPtr.Zero, workingDirectory, ref startup, out process))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateProcessW(CREATE_SUSPENDED) failed");
            bool resumed = false;
            try {
                if (!AssignProcessToJobObject(job, process.hProcess))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "AssignProcessToJobObject failed");
                if (ResumeThread(process.hThread) == UInt32.MaxValue)
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "ResumeThread failed");
                resumed = true;
                return checked((int)process.dwProcessId);
            } finally {
                if (!resumed) TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }

        public JobProcessStartSnapshot StopAndSnapshot(int timeoutMilliseconds) {
            lock (sync) {
                if (stopped) return finalSnapshot;
                stopped = true;
            }
            string terminationError = null;
            if (!TerminateJobObject(job, 1))
                terminationError = new Win32Exception(Marshal.GetLastWin32Error(), "TerminateJobObject failed").Message;

            int boundedTimeoutMilliseconds = Math.Max(1, timeoutMilliseconds);
            var stopwatch = Stopwatch.StartNew();
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = new JOBOBJECT_BASIC_ACCOUNTING_INFORMATION();
            string accountingError = null;
            bool reconciled = false;
            do {
                try {
                    accounting = QueryAccounting();
                    int notificationCount;
                    lock (sync) notificationCount = records.Count;
                    reconciled = accounting.ActiveProcesses == 0 && notificationCount == accounting.TotalProcesses;
                    if (reconciled) break;
                } catch (Exception error) {
                    accountingError = error.Message;
                    break;
                }
                Thread.Sleep(10);
            } while (stopwatch.ElapsedMilliseconds < boundedTimeoutMilliseconds);

            bool allProcessesExited = accountingError == null && accounting.ActiveProcesses == 0;
            bool terminationTimedOut = !allProcessesExited && stopwatch.ElapsedMilliseconds >= boundedTimeoutMilliseconds;

            if (!PostQueuedCompletionStatus(completionPort, 0, StopCompletionKey, IntPtr.Zero) && accountingError == null)
                accountingError = new Win32Exception(Marshal.GetLastWin32Error(), "Unable to stop completion-port monitor").Message;
            int monitorJoinMilliseconds = Math.Max(1,
                boundedTimeoutMilliseconds - checked((int)Math.Min(stopwatch.ElapsedMilliseconds, Int32.MaxValue)));
            if (monitorThread != null && !monitorThread.Join(monitorJoinMilliseconds) && accountingError == null)
                accountingError = "Completion-port monitor did not stop within the deadline.";

            JobProcessStartRecord[] snapshotRecords;
            lock (sync) snapshotRecords = records.ToArray();
            UInt32 unresolved = 0;
            foreach (var record in snapshotRecords) if (!record.MetadataResolved) ++unresolved;
            reconciled = accountingError == null && accounting.ActiveProcesses == 0 &&
                snapshotRecords.Length == accounting.TotalProcesses;
            var reasons = new List<string>();
            if (terminationError != null) reasons.Add(terminationError);
            if (accountingError != null) reasons.Add(accountingError);
            if (monitorError != null) reasons.Add(monitorError);
            if (accounting.ActiveProcesses != 0) reasons.Add("The Job Object still had active processes after termination.");
            if (terminationTimedOut) reasons.Add("The Job Object process-exit deadline expired.");
            if (snapshotRecords.Length != accounting.TotalProcesses)
                reasons.Add("Job accounting TotalProcesses did not equal the NEW_PROCESS notification count.");
            if (unresolved != 0) reasons.Add("At least one Job Object process start had unresolved executable metadata.");
            finalSnapshot = new JobProcessStartSnapshot {
                Records = snapshotRecords,
                NotificationCount = checked((UInt32)snapshotRecords.Length),
                TotalProcesses = accounting.TotalProcesses,
                ActiveProcesses = accounting.ActiveProcesses,
                UnresolvedProcessCount = unresolved,
                AccountingReconciled = reconciled,
                AllProcessesExited = allProcessesExited,
                TerminationWaitMilliseconds = stopwatch.ElapsedMilliseconds,
                TerminationTimedOut = terminationTimedOut,
                Measured = reasons.Count == 0 && reconciled,
                Reason = reasons.Count == 0 ? null : String.Join(" ", reasons.ToArray())
            };
            return finalSnapshot;
        }

        private void Monitor() {
            try {
                while (true) {
                    UInt32 message;
                    UIntPtr key;
                    IntPtr value;
                    bool ok = GetQueuedCompletionStatus(completionPort, out message, out key, out value, UInt32.MaxValue);
                    if (key.Equals(StopCompletionKey)) return;
                    if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error(), "GetQueuedCompletionStatus failed");
                    if (key.Equals(JobCompletionKey) && message == JOB_OBJECT_MSG_NEW_PROCESS) {
                        AddProcessStart(checked((int)value.ToInt64()));
                    }
                }
            } catch (Exception error) {
                monitorError = error.Message;
            }
        }

        private void AddProcessStart(int processId) {
            var record = new JobProcessStartRecord {
                ProcessId = processId,
                ParentProcessId = 0,
                ProcessName = String.Empty,
                ImagePath = String.Empty,
                TimeCreated = DateTime.UtcNow.ToString("o"),
                MetadataResolved = false,
                MetadataError = 0
            };
            IntPtr process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, checked((UInt32)processId));
            if (process == IntPtr.Zero) {
                record.MetadataError = Marshal.GetLastWin32Error();
            } else {
                try {
                    var image = new StringBuilder(32768);
                    UInt32 length = checked((UInt32)image.Capacity);
                    PROCESS_BASIC_INFORMATION basic;
                    int returnLength;
                    int queryStatus = NtQueryInformationProcess(process, 0, out basic,
                        Marshal.SizeOf(typeof(PROCESS_BASIC_INFORMATION)), out returnLength);
                    bool imageResolved = QueryFullProcessImageName(process, 0, image, ref length);
                    if (queryStatus == 0) record.ParentProcessId = checked((int)basic.InheritedFromUniqueProcessId.ToInt64());
                    if (imageResolved) {
                        record.ImagePath = image.ToString();
                        record.ProcessName = Path.GetFileName(record.ImagePath);
                    }
                    record.MetadataResolved = queryStatus == 0 && imageResolved && !String.IsNullOrWhiteSpace(record.ProcessName);
                    if (!record.MetadataResolved) record.MetadataError = imageResolved ? queryStatus : Marshal.GetLastWin32Error();
                } finally {
                    CloseHandle(process);
                }
            }
            lock (sync) records.Add(record);
        }

        private JOBOBJECT_BASIC_ACCOUNTING_INFORMATION QueryAccounting() {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
            UInt32 returned;
            if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, out accounting,
                    (UInt32)Marshal.SizeOf(typeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION)), out returned))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "QueryInformationJobObject accounting failed");
            return accounting;
        }

        private static string QuoteArgument(string value) {
            if (value.Length != 0 && value.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return value;
            var result = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char character in value) {
                if (character == '\\') { ++backslashes; continue; }
                if (character == '"') {
                    result.Append('\\', backslashes * 2 + 1).Append('"');
                    backslashes = 0;
                    continue;
                }
                result.Append('\\', backslashes).Append(character);
                backslashes = 0;
            }
            result.Append('\\', backslashes * 2).Append('"');
            return result.ToString();
        }

        public void Dispose() {
            try {
                if (!stopped) StopAndSnapshot(5000);
            } finally {
                DisposeNativeHandles();
            }
            GC.SuppressFinalize(this);
        }

        private void DisposeNativeHandles() {
            if (completionPort != IntPtr.Zero) { CloseHandle(completionPort); completionPort = IntPtr.Zero; }
            if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
        }
    }
}
'@
}

function Start-ProcessStartTrace {
	Initialize-WindowsJobProcessStartTracker
	$tracker = [Mumble.WindowsPerformance.JobProcessStartTracker]::new()
	return [pscustomobject]@{
		tracker = $tracker
		mechanism = "Windows Job Object completion port reconciled with Job accounting"
		poll_interval_ms = 0
		primary_error = $null
		measured = $true
		stopped = $false
		stop_result = $null
	}
}

function Start-ProcessInStartTraceJob {
	param(
		[Parameter(Mandatory = $true)]$Trace,
		[Parameter(Mandatory = $true)][string]$FilePath,
		[Parameter(Mandatory = $true)][string[]]$ArgumentList,
		[Parameter(Mandatory = $true)][string]$WorkingDirectory
	)
	$processId = $Trace.tracker.StartSuspended($FilePath, $ArgumentList, $WorkingDirectory)
	return [Diagnostics.Process]::GetProcessById($processId)
}

function Stop-ProcessStartTrace {
	param([Parameter(Mandatory = $true)]$Trace)
	if ([bool]$Trace.stopped) { return $Trace.stop_result }
	try {
		$snapshot = $Trace.tracker.StopAndSnapshot(5000)
		$records = @($snapshot.Records | ForEach-Object {
			[pscustomobject][ordered]@{
				process_id = [int]$_.ProcessId
				parent_process_id = [int]$_.ParentProcessId
				process_name = [string]$_.ProcessName
				image_path = [string]$_.ImagePath
				time_created = [string]$_.TimeCreated
				metadata_resolved = [bool]$_.MetadataResolved
				metadata_error = [int]$_.MetadataError
				job_descendant = [bool]$_.JobDescendant
			}
		})
		$result = [pscustomobject][ordered]@{
			measured = [bool]$snapshot.Measured
			reason = [string]$snapshot.Reason
			records = $records
			notification_count = [int]$snapshot.NotificationCount
			job_total_processes = [int]$snapshot.TotalProcesses
			job_active_processes_after_termination = [int]$snapshot.ActiveProcesses
			all_processes_exited = [bool]$snapshot.AllProcessesExited
			termination_wait_ms = [int64]$snapshot.TerminationWaitMilliseconds
			termination_timed_out = [bool]$snapshot.TerminationTimedOut
			unresolved_process_count = [int]$snapshot.UnresolvedProcessCount
			accounting_reconciled = [bool]$snapshot.AccountingReconciled
		}
		$Trace.stop_result = $result
		$Trace.stopped = $true
		return $result
	} finally {
		$Trace.tracker.Dispose()
	}
}

function Wait-FrozenProfileFilesReady {
	param(
		[Parameter(Mandatory = $true)][string[]]$Paths,
		[int]$TimeoutMilliseconds = 10000,
		[int]$PollIntervalMilliseconds = 50,
		[int]$RequiredStablePolls = 2
	)
	if ($TimeoutMilliseconds -lt 1) { throw "TimeoutMilliseconds must be positive." }
	if ($PollIntervalMilliseconds -lt 1) { throw "PollIntervalMilliseconds must be positive." }
	if ($RequiredStablePolls -lt 1) { throw "RequiredStablePolls must be positive." }
	$normalizedPaths = @($Paths | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
		ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique)
	if ($normalizedPaths.Count -eq 0) { throw "At least one frozen-profile path is required." }

	$stopwatch = [Diagnostics.Stopwatch]::StartNew()
	$attempts = 0
	$stablePolls = 0
	$previousSignature = $null
	$lastFailures = @()
	do {
		++$attempts
		$failures = [Collections.Generic.List[string]]::new()
		$signatureRows = [Collections.Generic.List[string]]::new()
		foreach ($path in $normalizedPaths) {
			if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
				$failures.Add("$path (missing)")
				continue
			}
			$stream = $null
			try {
				# FileShare.None proves that no terminated client/helper still owns a
				# database or log handle before hashing or recursive cleanup begins.
				$stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
				$info = [IO.FileInfo]::new($path)
				$signatureRows.Add("$path|$($stream.Length)|$($info.LastWriteTimeUtc.Ticks)")
			} catch {
				$failures.Add("$path ($($_.Exception.Message))")
			} finally {
				if ($null -ne $stream) { $stream.Dispose() }
			}
		}
		$lastFailures = @($failures)
		if ($failures.Count -eq 0) {
			$signature = $signatureRows -join "`n"
			$stablePolls = if ($signature -ceq $previousSignature) { $stablePolls + 1 } else { 1 }
			$previousSignature = $signature
			if ($stablePolls -ge $RequiredStablePolls) {
				return [pscustomobject][ordered]@{
					ready = $true
					reason = $null
					waited_ms = [int64]$stopwatch.ElapsedMilliseconds
					attempts = $attempts
					stable_polls = $stablePolls
					paths = $normalizedPaths
				}
			}
		} else {
			$stablePolls = 0
			$previousSignature = $null
		}
		$remaining = $TimeoutMilliseconds - [int]$stopwatch.ElapsedMilliseconds
		if ($remaining -gt 0) { Start-Sleep -Milliseconds ([Math]::Min($PollIntervalMilliseconds, $remaining)) }
	} while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMilliseconds)

	return [pscustomobject][ordered]@{
		ready = $false
		reason = "Frozen-profile files did not become exclusively readable and stable within $TimeoutMilliseconds ms: $($lastFailures -join '; ')"
		waited_ms = [int64]$stopwatch.ElapsedMilliseconds
		attempts = $attempts
		stable_polls = $stablePolls
		paths = $normalizedPaths
	}
}

function Remove-IsolatedPerformanceStateRoot {
	param(
		[Parameter(Mandatory = $true)][string]$RootPath,
		[Parameter(Mandatory = $true)][string]$AllowedParentPath,
		[int]$TimeoutMilliseconds = 10000,
		[int]$PollIntervalMilliseconds = 100
	)
	$rootFullPath = [IO.Path]::GetFullPath($RootPath).TrimEnd('\', '/')
	$parentFullPath = [IO.Path]::GetFullPath($AllowedParentPath).TrimEnd('\', '/')
	$parentPrefix = $parentFullPath + [IO.Path]::DirectorySeparatorChar
	if ($rootFullPath -ceq $parentFullPath -or
		-not $rootFullPath.StartsWith($parentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to clean an isolated state root outside the output directory: $rootFullPath"
	}
	if (-not (Test-Path -LiteralPath $rootFullPath)) {
		return [pscustomobject][ordered]@{ removed = $true; reason = $null; waited_ms = 0; attempts = 0 }
	}

	$stopwatch = [Diagnostics.Stopwatch]::StartNew()
	$criticalFiles = @(Get-ChildItem -LiteralPath $rootFullPath -File -Recurse -ErrorAction Stop |
		Where-Object { $_.Name -in @('mumble.sqlite', 'Console.txt') } | ForEach-Object { $_.FullName })
	if ($criticalFiles.Count -gt 0) {
		$ready = Wait-FrozenProfileFilesReady -Paths $criticalFiles -TimeoutMilliseconds $TimeoutMilliseconds `
			-PollIntervalMilliseconds ([Math]::Min($PollIntervalMilliseconds, 100))
		if (-not [bool]$ready.ready) {
			return [pscustomobject][ordered]@{
				removed = $false
				reason = [string]$ready.reason
				waited_ms = [int64]$stopwatch.ElapsedMilliseconds
				attempts = 0
			}
		}
	}

	$attempts = 0
	$lastReason = $null
	do {
		++$attempts
		try {
			Remove-Item -LiteralPath $rootFullPath -Recurse -Force -ErrorAction Stop
			if (-not (Test-Path -LiteralPath $rootFullPath)) {
				return [pscustomobject][ordered]@{
					removed = $true
					reason = $null
					waited_ms = [int64]$stopwatch.ElapsedMilliseconds
					attempts = $attempts
				}
			}
		} catch {
			$lastReason = $_.Exception.Message
		}
		$remaining = $TimeoutMilliseconds - [int]$stopwatch.ElapsedMilliseconds
		if ($remaining -gt 0) { Start-Sleep -Milliseconds ([Math]::Min($PollIntervalMilliseconds, $remaining)) }
	} while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMilliseconds)
	return [pscustomobject][ordered]@{
		removed = $false
		reason = if ([string]::IsNullOrWhiteSpace($lastReason)) { "The isolated state root still existed after the cleanup deadline." } else { $lastReason }
		waited_ms = [int64]$stopwatch.ElapsedMilliseconds
		attempts = $attempts
	}
}

function Get-LatchedWebEngineProcessStarts {
	param(
		[int]$RootProcessId,
		[AllowEmptyCollection()][object[]]$ProcessStartRecords
	)
	$parentByPid = @{}
	foreach ($record in $ProcessStartRecords) {
		$parentByPid[[string][int]$record.process_id] = [int]$record.parent_process_id
	}
	$latched = @()
	foreach ($record in @($ProcessStartRecords | Where-Object { $_.process_name -ieq 'QtWebEngineProcess.exe' })) {
		$jobDescendantProperty = $record.PSObject.Properties["job_descendant"]
		if ($null -ne $jobDescendantProperty -and [bool]$jobDescendantProperty.Value) {
			$latched += $record
			continue
		}
		$parent = [int]$record.parent_process_id
		$visited = [Collections.Generic.HashSet[int]]::new()
		$isDescendant = $false
		while ($parent -gt 0 -and $visited.Add($parent)) {
			if ($parent -eq $RootProcessId) { $isDescendant = $true; break }
			$key = [string]$parent
			if (-not $parentByPid.ContainsKey($key)) { break }
			$parent = [int]$parentByPid[$key]
		}
		if ($isDescendant) { $latched += $record }
	}
	return $latched
}

function Invoke-QmlAutomationCommand {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][hashtable]$Request,
		[int]$TimeoutMilliseconds = 8000
	)
	$Request["token"] = $Token
	$json = $Request | ConvertTo-Json -Depth 20 -Compress
	$client = [Net.Sockets.TcpClient]::new()
	$async = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
	if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
		$client.Dispose()
		throw "Timed out connecting to QML automation on port $Port."
	}
	$client.EndConnect($async)
	try {
		$client.ReceiveTimeout = $TimeoutMilliseconds
		$client.SendTimeout = $TimeoutMilliseconds
		$stream = $client.GetStream()
		$writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
		$reader = [IO.StreamReader]::new($stream, [Text.UTF8Encoding]::new($false))
		try {
			$writer.NewLine = "`n"
			$writer.WriteLine($json)
			$writer.Flush()
			$line = $reader.ReadLine()
			if ([string]::IsNullOrWhiteSpace($line)) { throw "QML automation returned an empty response." }
			$response = $line | ConvertFrom-Json
			if (-not [bool]$response.ok) {
				$errorText = if ($response.PSObject.Properties.Name -contains "error") { [string]$response.error } else { "unknown error" }
				throw "QML automation command '$($Request.command)' failed: $errorText"
			}
			return $response
		} finally {
			$writer.Dispose()
			$reader.Dispose()
		}
	} finally { $client.Dispose() }
}

function Wait-QmlAutomation {
	param([int]$Port, [string]$Token, [int]$TimeoutSeconds)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$lastError = ""
	do {
		try { return Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "ping" } -TimeoutMilliseconds 1000 }
		catch { $lastError = $_.Exception.Message; Start-Sleep -Milliseconds 100 }
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for QML automation on port $Port. Last error: $lastError"
}

function Get-QmlReadinessState {
	param([int]$Port, [string]$Token)
	return Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "qmlReadinessState" }
}

function Get-QmlReadinessFingerprint {
	param([Parameter(Mandatory = $true)]$Readiness)
	return [ordered]@{
		connected = [bool]$Readiness.connected
		active_scope_token = [string]$Readiness.activeScopeToken
		room_count = [int]$Readiness.roomCount
		participant_count = [int]$Readiness.participantCount
		message_count = [int]$Readiness.messageCount
		dialog_open = [bool]$Readiness.dialogOpen
		media_active = [bool]$Readiness.mediaActive
		window_visible = [bool]$Readiness.windowVisible
		window_exposed = [bool]$Readiness.windowExposed
		window_visibility = [int]$Readiness.windowVisibility
	} | ConvertTo-Json -Compress
}

function Wait-QmlReadinessQuiescence {
	param([int]$Port, [string]$Token, [int]$TimeoutMilliseconds = 5000, [int]$RequiredStablePolls = 5)
	$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
	$previous = $null
	$stablePolls = 0
	do {
		$readiness = Get-QmlReadinessState -Port $Port -Token $Token
		$fingerprint = Get-QmlReadinessFingerprint -Readiness $readiness
		if ($fingerprint -ceq $previous) { ++$stablePolls } else { $stablePolls = 0; $previous = $fingerprint }
		if ($stablePolls -ge $RequiredStablePolls) { return $stablePolls }
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	return 0
}

function Assert-QmlMeasurementWindowReady {
	param(
		[int]$Port,
		[string]$Token,
		[Parameter(Mandatory = $true)][Diagnostics.Process]$Process,
		[string]$Phase,
		[int]$TimeoutMilliseconds = 5000
	)
	Set-QmlPerformanceWindowTopmost -Process $Process -Topmost $true
	$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
	$last = $null
	do {
		$last = Get-QmlReadinessState -Port $Port -Token $Token
		if ([bool]$last.windowVisible -and [bool]$last.windowExposed -and
			[int]$last.windowVisibility -notin @(0, 3)) { return $last }
		Start-Sleep -Milliseconds 25
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "The QML benchmark window was not visible and exposed for phase '$Phase': $($last | ConvertTo-Json -Compress -Depth 6)"
}

function Wait-QmlInteractiveShell {
	param([int]$Port, [string]$Token, [int]$TimeoutSeconds)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$lastState = $null
	do {
		try {
			$lastState = Get-QmlReadinessState -Port $Port -Token $Token
			$properties = @($lastState.PSObject.Properties.Name)
			if ($properties -contains "frontend" -and [string]$lastState.frontend -eq "qml" -and
				$properties -contains "windowReady" -and [bool]$lastState.windowReady -and
				$properties -contains "mainCaptureReady" -and [bool]$lastState.mainCaptureReady) {
				return $lastState
			}
		} catch { }
		Start-Sleep -Milliseconds 25
	} while ([DateTime]::UtcNow -lt $deadline)
	$diagnostic = if ($null -eq $lastState) { "no readiness response" } else { $lastState | ConvertTo-Json -Compress -Depth 8 }
	throw "Timed out waiting for an interactive QML shell (windowReady + mainCaptureReady). Last state: $diagnostic"
}

function Get-QmlSnapshot {
	param([int]$Port, [string]$Token)
	return (Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "snapshot" }).snapshot
}

function Resolve-QmlRoomScopeToken {
	param([AllowNull()]$Room)
	if ($null -eq $Room) { return $null }

	# RoomModel's typed scopeToken is authoritative. Older automation snapshots may
	# expose the same canonical value as token or id, but namespaced row IDs such as
	# "text:3:0" are model identities and cannot be passed to scope commands.
	$propertyNames = @("scopeToken", "token", "id")
	foreach ($propertyName in $propertyNames) {
		$property = $Room.PSObject.Properties[$propertyName]
		if ($null -eq $property) { continue }
		$candidate = [string]$property.Value
		if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
		$candidate = $candidate.Trim()
		if ($candidate -match '^-?\d+:\d+$') { return $candidate }
	}
	return $null
}

function Wait-ConnectedRoomState {
	param([int]$Port, [string]$Token, [int]$TimeoutSeconds)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do {
		$snapshot = Get-QmlSnapshot -Port $Port -Token $Token
		$rooms = @($snapshot.voiceRooms) + @($snapshot.textRooms)
		$tokens = @($rooms | ForEach-Object { Resolve-QmlRoomScopeToken -Room $_ } |
			Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
		if ([bool]$snapshot.app.connected -and $tokens.Count -ge 2) {
			return [pscustomobject]@{ snapshot = $snapshot; tokens = $tokens }
		}
		Start-Sleep -Milliseconds 200
	} while ([DateTime]::UtcNow -lt $deadline)
	return $null
}

function Wait-SelectedScope {
	param([int]$Port, [string]$Token, [string]$ScopeToken, [int]$TimeoutMilliseconds = 5000)
	$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
	do {
		# The full automation snapshot contains the active timeline, rich-preview
		# metadata and image-provider URLs. Serializing that payload on the UI thread
		# after every measured room switch can delay the very frame this probe is
		# waiting for and therefore turn automation overhead into input latency and
		# heartbeat stalls. qmlReadinessState is the bounded controller-level probe
		# intended for polling and exposes the same authoritative active scope token.
		$readiness = Get-QmlReadinessState -Port $Port -Token $Token
		if ([string]$readiness.activeScopeToken -eq $ScopeToken) {
			return $true
		}
		Start-Sleep -Milliseconds 20
	} while ([DateTime]::UtcNow -lt $deadline)
	return $false
}

function Wait-QmlInputSample {
	param([int]$Port, [string]$Token, [int]$MinimumCount, [int]$TimeoutMilliseconds = 5000)
	$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
	do {
		$performance = (Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
			command = "qmlPerformanceSnapshot"
		}).performance
		if ([int]$performance.inputSampleCount -ge $MinimumCount) { return $true }
		Start-Sleep -Milliseconds 10
	} while ([DateTime]::UtcNow -lt $deadline)
	return $false
}

function Invoke-RoomSwitchWorkload {
	param([int]$Port, [string]$Token, [string[]]$ScopeTokens, [int]$Iterations)
	if ($ScopeTokens.Count -lt 2) { throw "Room-switch workload requires at least two connected room scopes." }
	for ($index = 0; $index -lt $Iterations; ++$index) {
		$scopeToken = $ScopeTokens[$index % $ScopeTokens.Count]
		$operationId = "room-switch:${index}:$([Guid]::NewGuid().ToString('N'))"
		$beforePerformance = (Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
			command = "qmlPerformanceSnapshot"
		}).performance
		$expectedInputSamples = [int]$beforePerformance.inputSampleCount + 1
		$select = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
			command = "qmlPerformanceSelectScope"; scopeToken = $scopeToken; operationId = $operationId
		}
		$measuredId = [string]$select.operationId
		if ([string]::IsNullOrWhiteSpace($measuredId)) { throw "qmlPerformanceSelectScope returned no operation ID." }
		if ($select.PSObject.Properties.Name -contains "handled" -and -not [bool]$select.handled) {
			throw "Room switch to '$scopeToken' was not handled."
		}
		if (-not (Wait-SelectedScope -Port $Port -Token $Token -ScopeToken $scopeToken)) {
			throw "Room switch to '$scopeToken' did not become observable."
		}
		# The monitor completes pending input from QQuickWindow::frameSwapped. Requiring the sample
		# before the explicit completion prevents a controller-only state change from looking rendered.
		if (-not (Wait-QmlInputSample -Port $Port -Token $Token -MinimumCount $expectedInputSamples)) {
			throw "Room switch '$scopeToken' produced no measured scene-graph frame."
		}
		# Exercise the explicit API too. This is intentionally idempotent after frameSwapped completion.
		Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
			command = "qmlPerformanceMarkVisual"; operationId = $measuredId
		} | Out-Null
	}
}

function Wait-QmlStateQuiescence {
	param([int]$Port, [string]$Token, [int]$TimeoutMilliseconds = 10000, [int]$RequiredStablePolls = 5)
	$deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
	$previous = $null
	$stablePolls = 0
	do {
		$snapshot = Get-QmlSnapshot -Port $Port -Token $Token
		$fingerprint = $snapshot | ConvertTo-Json -Depth 30 -Compress
		if ($fingerprint -ceq $previous) { ++$stablePolls } else { $stablePolls = 0; $previous = $fingerprint }
		if ($stablePolls -ge $RequiredStablePolls) { return $stablePolls }
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	return 0
}

function Get-QmlPerformanceSnapshot {
	param([int]$Port, [string]$Token)
	return (Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "qmlPerformanceSnapshot" }).performance
}

function Get-QmlModelResetAnalysis {
	param(
		[AllowNull()]$Performance,
		[string[]]$RequiredCounters
	)
	$counts = [ordered]@{}
	$missing = [Collections.Generic.List[string]]::new()
	if ($null -eq $Performance -or -not ($Performance.PSObject.Properties.Name -contains "modelResetCounts")) {
		return [pscustomobject]@{
			measured = $false
			reason = "performance snapshot does not expose modelResetCounts"
			total = $null
			counts = $counts
		}
	}
	$source = $Performance.modelResetCounts
	foreach ($counter in $RequiredCounters) {
		$property = $source.PSObject.Properties[$counter]
		if ($null -eq $property) {
			$missing.Add($counter)
			continue
		}
		$counts[$counter] = [int64]$property.Value
	}
	if ($missing.Count -gt 0) {
		return [pscustomobject]@{
			measured = $false
			reason = "modelResetCounts is missing: $($missing -join ', ')"
			total = $null
			counts = $counts
		}
	}
	$total = 0L
	foreach ($value in $counts.Values) { $total += [int64]$value }
	return [pscustomobject]@{
		measured = $true
		reason = $null
		total = $total
		counts = $counts
	}
}

function Get-QmlSyncUiOperationAnalysis {
	param([AllowNull()]$Performance)
	$requiredCounters = @("network", "plugin", "file")
	$counts = [ordered]@{}
	if ($null -eq $Performance -or
		-not ($Performance.PSObject.Properties.Name -contains "syncUiOperationViolationCounts")) {
		return [pscustomobject]@{
			measured = $false
			reason = "performance snapshot does not expose syncUiOperationViolationCounts"
			passed = $false
			declared_passed = $null
			total = $null
			counts = $counts
		}
	}
	$source = $Performance.syncUiOperationViolationCounts
	if ($null -eq $source) {
		return [pscustomobject]@{
			measured = $false
			reason = "syncUiOperationViolationCounts is null"
			passed = $false
			declared_passed = $null
			total = $null
			counts = $counts
		}
	}
	$observedCounters = @($source.PSObject.Properties.Name)
	$missingCounters = @($requiredCounters | Where-Object { $observedCounters -notcontains $_ })
	$unexpectedCounters = @($observedCounters | Where-Object { $requiredCounters -notcontains $_ })
	$passProperty = $Performance.PSObject.Properties["noSyncUiOperationsPassed"]
	if ($missingCounters.Count -gt 0 -or $unexpectedCounters.Count -gt 0 -or $null -eq $passProperty -or
		$passProperty.Value -isnot [bool]) {
		$problems = [Collections.Generic.List[string]]::new()
		if ($missingCounters.Count -gt 0) { $problems.Add("missing counters: $($missingCounters -join ', ')") }
		if ($unexpectedCounters.Count -gt 0) { $problems.Add("unexpected counters: $($unexpectedCounters -join ', ')") }
		if ($null -eq $passProperty) {
			$problems.Add("noSyncUiOperationsPassed is missing")
		} elseif ($passProperty.Value -isnot [bool]) {
			$problems.Add("noSyncUiOperationsPassed is not Boolean")
		}
		return [pscustomobject]@{
			measured = $false
			reason = $problems -join "; "
			passed = $false
			declared_passed = $null
			total = $null
			counts = $counts
		}
	}
	$total = 0L
	foreach ($counter in $requiredCounters) {
		$value = [int64]$source.PSObject.Properties[$counter].Value
		if ($value -lt 0) {
			return [pscustomobject]@{
				measured = $false
				reason = "syncUiOperationViolationCounts.$counter must not be negative"
				passed = $false
				declared_passed = $null
				total = $null
				counts = $counts
			}
		}
		$counts[$counter] = $value
		$total += $value
	}
	$declaredPassed = [bool]$passProperty.Value
	return [pscustomobject]@{
		measured = $true
		reason = $null
		passed = $declaredPassed -and $total -eq 0
		declared_passed = $declaredPassed
		total = $total
		counts = $counts
	}
}

$savedEnvironment = @{
	MUMBLE_CHAT_PERF_TRACE = $env:MUMBLE_CHAT_PERF_TRACE
	MUMBLE_CHAT_PERF_TRACE_PATH = $env:MUMBLE_CHAT_PERF_TRACE_PATH
	MUMBLE_MODERN_AUTOMATION_PORT = $env:MUMBLE_MODERN_AUTOMATION_PORT
	MUMBLE_MODERN_AUTOMATION_TOKEN = $env:MUMBLE_MODERN_AUTOMATION_TOKEN
}
$traceFile = $null
$measurements = @()
$notMeasured = [Collections.Generic.List[string]]::new()
$machineFingerprint = Get-WindowsReferenceMachineFingerprint
$frozenProfileSeed = Get-FrozenProfileSeed -SourceConfigPath $configFilePath
$validatedWebBaseline = Get-ValidatedWebPerformanceBaseline -Path $WebBaselinePath `
	-MachineFingerprint $machineFingerprint -FrozenProfileSeed $frozenProfileSeed `
	-ExpectedSchemaVersion $performanceSchemaVersion -ExpectedContractId $performanceContractId
$baselineSummary = $validatedWebBaseline.summary
$baselineStartupMedian = [double]$validatedWebBaseline.startup_median_ms
$baselineIdleWorkingSetMedian = [double]$validatedWebBaseline.idle_working_set_median_bytes
$baselineDiagnostics = $validatedWebBaseline.diagnostics
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$isolatedStateRoot = Join-Path $outputDirectory ("qml-performance-frozen-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $isolatedStateRoot | Out-Null
$sourceProfileCheck = $null
$isolationCleanup = [ordered]@{ removed = $false; reason = $null }

try {
	if ([string]::IsNullOrWhiteSpace($ChatPerfTracePath)) {
		$notMeasured.Add("chat_perf_trace: ChatPerfTracePath was not provided")
	} else {
		$traceFile = [IO.Path]::GetFullPath($ChatPerfTracePath)
		New-Item -ItemType Directory -Force -Path (Split-Path -Parent $traceFile) | Out-Null
		Remove-Item -LiteralPath $traceFile -Force -ErrorAction SilentlyContinue
		$env:MUMBLE_CHAT_PERF_TRACE = "1"
		$env:MUMBLE_CHAT_PERF_TRACE_PATH = $traceFile
	}

	for ($run = 1; $run -le $Runs; ++$run) {
		$runProfile = New-FrozenRunProfile -Seed $frozenProfileSeed `
			-RunDirectory (Join-Path $isolatedStateRoot ("run-{0:D2}" -f $run)) -Run $run
		$runPort = if ($AutomationPort -gt 0) { $AutomationPort } else { Get-FreeTcpPort }
		$runToken = if ([string]::IsNullOrWhiteSpace($AutomationToken)) { [Guid]::NewGuid().ToString("N") } else { $AutomationToken }
		$env:MUMBLE_MODERN_AUTOMATION_PORT = [string]$runPort
		$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $runToken

		$processStartTrace = Start-ProcessStartTrace
		$processStartTraceStopped = $false
		$process = $null
		$runMeasurement = $null
		try {
			$stopwatch = [Diagnostics.Stopwatch]::StartNew()
			$process = Start-ProcessInStartTraceJob -Trace $processStartTrace -FilePath $executablePath `
				-ArgumentList @("--multiple", "--config", $runProfile.config_path) `
				-WorkingDirectory (Split-Path -Parent $executablePath)
			$deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
			do {
				Start-Sleep -Milliseconds 25
				$process.Refresh()
				if ($process.HasExited) { throw "Mumble exited during startup with code $($process.ExitCode)." }
			} while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
			if ($process.MainWindowHandle -eq 0) { throw "Timed out waiting for the QML top-level window." }
			$startupWindowDiagnosticMilliseconds = $stopwatch.Elapsed.TotalMilliseconds
			Wait-QmlAutomation -Port $runPort -Token $runToken -TimeoutSeconds $StartupTimeoutSeconds | Out-Null
			$interactiveState = Wait-QmlInteractiveShell -Port $runPort -Token $runToken -TimeoutSeconds $StartupTimeoutSeconds
			$startupInteractiveMilliseconds = $stopwatch.Elapsed.TotalMilliseconds
			Assert-QmlMeasurementWindowReady -Port $runPort -Token $runToken -Process $process -Phase "startup" | Out-Null

			$ready = Wait-ConnectedRoomState -Port $runPort -Token $runToken -TimeoutSeconds $WorkloadReadyTimeoutSeconds
			$roomSwitchMeasured = $false
			$roomSwitchReason = $null
			$roomWarmup = [ordered]@{ measured = $false; scope_count = 0; stable_polls = 0; reason = $null }
			$connectedIdle = [ordered]@{
				measured = $false
				reason = $null
				stable_polls = 0
				stable_polls_before = 0
				stable_polls_after = 0
				media_active_before = $null
				media_active_after = $null
				process_metrics = $null
				module_residency = $null
				performance = $null
			}
			if ($null -eq $ready) {
				$roomSwitchReason = "connected=true and at least two room scopes were not observable"
				$notMeasured.Add("run $run room_switch: $roomSwitchReason")
				$connectedIdle.reason = $roomSwitchReason
				$notMeasured.Add("run $run connected_idle: $roomSwitchReason")
			} else {
				$idleSamplingActive = $false
				try {
					Assert-QmlMeasurementWindowReady -Port $runPort -Token $runToken -Process $process -Phase "connected-idle" | Out-Null
					$idleStablePolls = Wait-QmlStateQuiescence -Port $runPort -Token $runToken
					if ($idleStablePolls -le 0) { throw "Connected controller state did not become quiescent before idle sampling." }
					$idleBefore = Get-QmlReadinessState -Port $runPort -Token $runToken
					if (-not ($idleBefore.PSObject.Properties.Name -contains "mediaActive")) {
						throw "qmlReadinessState did not expose mediaActive before idle sampling."
					}
					if (-not [bool]$idleBefore.connected) { throw "The client disconnected before idle sampling." }
					if ([bool]$idleBefore.mediaActive) { throw "A media session was active before connected-idle sampling." }
					Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
					Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
					$idleSamplingActive = $true
					Start-Sleep -Seconds $IdleSeconds
					# Poll only the bounded controller/readiness fingerprint while the
					# heartbeat is measured. Full timeline/model serialization belongs
					# outside the sample and would otherwise create its own UI stall.
					$idleStablePollsAfter = Wait-QmlReadinessQuiescence -Port $runPort -Token $runToken
					if ($idleStablePollsAfter -le 0) { throw "Connected controller state did not remain quiescent through idle sampling." }
					$idleAfter = Get-QmlReadinessState -Port $runPort -Token $runToken
					if (-not [bool]$idleAfter.connected) { throw "The client disconnected during connected-idle sampling." }
					if ([bool]$idleAfter.mediaActive) { throw "A media session became active during connected-idle sampling." }
					Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
					$idleSamplingActive = $false
					$connectedIdle.performance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
					$connectedIdle.measured = $true
					$connectedIdle.stable_polls = [Math]::Min($idleStablePolls, $idleStablePollsAfter)
					$connectedIdle.stable_polls_before = $idleStablePolls
					$connectedIdle.stable_polls_after = $idleStablePollsAfter
					$connectedIdle.media_active_before = [bool]$idleBefore.mediaActive
					$connectedIdle.media_active_after = [bool]$idleAfter.mediaActive
					$connectedIdle.process_metrics = Get-ProcessTreeMetrics -RootProcessId $process.Id
					$connectedIdle.module_residency = Get-WebEngineModuleResidency -RootProcessId $process.Id
				} catch {
					$connectedIdle.reason = $_.Exception.Message
					$notMeasured.Add("run $run connected_idle: $($connectedIdle.reason)")
				} finally {
					if ($idleSamplingActive) {
						try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null } catch { }
					}
				}
				try {
					# Cold history/preview hydration belongs to startup preparation, while the
					# latency gate measures steady-state navigation. Exercise every measured
					# scope once, then require controller snapshots to settle. ChatPerfTrace
					# remains active across warmup, so synchronous backend work is still gated.
					Invoke-RoomSwitchWorkload -Port $runPort -Token $runToken -ScopeTokens $ready.tokens -Iterations $ready.tokens.Count
					$stablePolls = Wait-QmlStateQuiescence -Port $runPort -Token $runToken
					if ($stablePolls -le 0) { throw "Connected controller state did not become quiescent after room warmup." }
					$roomWarmup.measured = $true
					$roomWarmup.scope_count = $ready.tokens.Count
					$roomWarmup.stable_polls = $stablePolls
				} catch {
					$roomWarmup.reason = $_.Exception.Message
					$roomSwitchReason = "room warmup failed: $($roomWarmup.reason)"
					$notMeasured.Add("run $run room_warmup: $($roomWarmup.reason)")
				}
			}

			Assert-QmlMeasurementWindowReady -Port $runPort -Token $runToken -Process $process -Phase "room-switch" | Out-Null
			Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
			Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
			if ($null -ne $ready -and $roomWarmup.measured) {
				try {
					Invoke-RoomSwitchWorkload -Port $runPort -Token $runToken -ScopeTokens $ready.tokens -Iterations $RoomSwitchIterations
					$roomSwitchMeasured = $true
				} catch {
					$roomSwitchReason = $_.Exception.Message
					$notMeasured.Add("run $run room_switch: $roomSwitchReason")
				}
			}
			Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
			$roomPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken

			$chatSeed = [pscustomobject]@{ measured = $false; response = $null; reason = $null }
			$chatScroll = [pscustomobject]@{ measured = $false; response = $null; reason = $null }
			$chatSeedPerformance = $null; $chatScrollPerformance = $null
			try {
				Assert-QmlMeasurementWindowReady -Port $runPort -Token $runToken -Process $process -Phase "chat-scroll" | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatSeedStart" } | Out-Null
				$seedDeadline = [DateTime]::UtcNow.AddSeconds(5); $seedStatus = $null
				$seedFingerprint = $null; $stableSeedPolls = 0
				do {
					$seedStatus = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatSeedStatus" }
					if ([bool]$seedStatus.ready) {
						$currentSeedFingerprint = $seedStatus.qml | ConvertTo-Json -Depth 8 -Compress
						if ($currentSeedFingerprint -ceq $seedFingerprint) { ++$stableSeedPolls } else {
							$seedFingerprint = $currentSeedFingerprint
							$stableSeedPolls = 1
						}
						if ($stableSeedPolls -ge 3) { break }
					} else {
						$seedFingerprint = $null
						$stableSeedPolls = 0
					}
					Start-Sleep -Milliseconds 16
				} while ([DateTime]::UtcNow -lt $seedDeadline)
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$chatSeedPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
				if (-not [bool]$seedStatus.ready -or $stableSeedPolls -lt 3) { throw "Chat seed did not become stably render-ready: $($seedStatus | ConvertTo-Json -Compress -Depth 8)" }
				$chatSeed = [pscustomobject]@{ measured = $true; response = $seedStatus; reason = $null }

				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				$scrollRun = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{
					command = "qmlPerformanceChatScrollRun"; stepCount = $requiredChatScrollInputSamples
				}
				if (-not [bool]$scrollRun.running -or -not [bool]$scrollRun.scroll.started) {
					throw "The frame-driven chat scroll refused to start: $($scrollRun | ConvertTo-Json -Compress -Depth 8)"
				}
				# The application advances the real QML ListView once per frameSwapped.
				# Polling observes completion only; transport/timer cadence cannot create
				# or collapse measured input samples.
				$scrollDeadline = [DateTime]::UtcNow.AddSeconds(5); $scrollStatus = $null
				do {
					$scrollStatus = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatScrollStatus" }
					if (-not [bool]$scrollStatus.running -and [bool]$scrollStatus.primed -and
						[string]::IsNullOrWhiteSpace([string]$scrollStatus.failureReason) -and
						[bool]$scrollStatus.scroll.moved -and
						[int]$scrollStatus.stepCount -ge $requiredChatScrollInputSamples -and
						[int]$scrollStatus.presentedFrameDelta -ge $requiredChatScrollInputSamples -and
						[int]$scrollStatus.performance.frameSampleCount -ge $requiredChatScrollInputSamples -and
						[int]$scrollStatus.performance.inputSampleCount -ge $requiredChatScrollInputSamples) { break }
					Start-Sleep -Milliseconds 10
				} while ([DateTime]::UtcNow -lt $scrollDeadline)
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$chatScrollPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
				if ([bool]$scrollStatus.running -or -not [bool]$scrollStatus.primed -or
					-not [string]::IsNullOrWhiteSpace([string]$scrollStatus.failureReason) -or
					-not [bool]$scrollStatus.scroll.moved -or
					[int]$scrollStatus.stepCount -lt $requiredChatScrollInputSamples -or
					[int]$scrollStatus.presentedFrameDelta -lt $requiredChatScrollInputSamples -or
					[int]$chatScrollPerformance.frameSampleCount -lt $requiredChatScrollInputSamples -or
					[int]$chatScrollPerformance.inputSampleCount -lt $requiredChatScrollInputSamples) {
					throw "Chat scroll did not complete with all required presented/input frames."
				}
				$chatScroll = [pscustomobject]@{ measured = $true; response = $scrollStatus; reason = $null }
			} catch {
				if (-not $chatSeed.measured) { $chatSeed.reason = $_.Exception.Message } else { $chatScroll.reason = $_.Exception.Message }
			} finally {
				try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null } catch { }
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatFinalize" } | Out-Null
			}

			$talkState = [pscustomobject]@{ measured = $false; response = $null; reason = $null }
			$talkPerformance = $null
			try {
				Assert-QmlMeasurementWindowReady -Port $runPort -Token $runToken -Process $process -Phase "talk-state" | Out-Null
				# Fixture replacement is setup, not steady-state talk churn. Start it before
				# resetting/beginning the measured phase so any setup model reset cannot be
				# mistaken for an incremental talk-state update.
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkStart" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				$talkRun = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{
					command = "qmlPerformanceTalkRun"; transitionCount = $TalkStateTransitions
				}
				if (-not [bool]$talkRun.running) { throw "The frame-driven talk-state workload did not start." }
				$talkDeadline = [DateTime]::UtcNow.AddSeconds(5); $talkStatus = $null
				do {
					$talkStatus = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkStatus" }
					if (-not [bool]$talkStatus.running -and [bool]$talkStatus.primed -and
						[int]$talkStatus.transitionCount -ge $TalkStateTransitions -and
						[int]$talkStatus.presentedFrameDelta -ge $TalkStateTransitions -and
						[int]$talkStatus.performance.frameSampleCount -ge $TalkStateTransitions -and
						[int]$talkStatus.performance.inputSampleCount -ge $TalkStateTransitions) { break }
					Start-Sleep -Milliseconds 10
				} while ([DateTime]::UtcNow -lt $talkDeadline)
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$talkPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
				if ([bool]$talkStatus.running -or -not [bool]$talkStatus.primed -or
					[int]$talkStatus.transitionCount -lt $TalkStateTransitions) {
					throw "Talk-state workload completed only $($talkStatus.transitionCount) of $TalkStateTransitions transitions."
				}
				if ([int]$talkStatus.presentedFrameDelta -lt $TalkStateTransitions -or
					[int]$talkPerformance.frameSampleCount -lt $TalkStateTransitions -or
					[int]$talkPerformance.inputSampleCount -lt $TalkStateTransitions) {
					throw "Talk-state workload produced fewer than $TalkStateTransitions presented/frame/input samples."
				}
				$talkState = [pscustomobject]@{ measured = $true; response = $talkStatus; reason = $null }
			} catch {
				$talkState.reason = $_.Exception.Message
			} finally {
				try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null } catch { }
				try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkFinalize" } | Out-Null } catch { }
			}
			$performance = $roomPerformance

			$preMediaStateMeasured = $false
			$preMediaStateReason = $null
			try {
				$preMediaState = Get-QmlReadinessState -Port $runPort -Token $runToken
				if (-not ($preMediaState.PSObject.Properties.Name -contains "mediaActive")) {
					throw "qmlReadinessState did not expose mediaActive after the workload."
				}
				if ([bool]$preMediaState.mediaActive) { throw "A media session became active in the no-media workload." }
				$preMediaStateMeasured = $true
			} catch {
				$preMediaStateReason = $_.Exception.Message
				$notMeasured.Add("run $run pre_media_state: $preMediaStateReason")
			}

			$finalProcessMetrics = Get-ProcessTreeMetrics -RootProcessId $process.Id
			$processStartRecords = @()
			$processStartTraceReason = $null
			$processStartSnapshot = $null
			try {
				$processStartSnapshot = Stop-ProcessStartTrace -Trace $processStartTrace
				$processStartRecords = @($processStartSnapshot.records)
				if (-not [bool]$processStartSnapshot.measured) {
					$processStartTraceReason = [string]$processStartSnapshot.reason
				}
			} catch {
				$processStartTraceReason = $_.Exception.Message
			} finally {
				$processStartTraceStopped = $true
			}
			$rootStartObserved = @($processStartRecords | Where-Object { [int]$_.process_id -eq $process.Id }).Count -gt 0
			$processStartTraceMeasured = $null -ne $processStartSnapshot -and [bool]$processStartSnapshot.measured -and
				[string]::IsNullOrWhiteSpace($processStartTraceReason) -and $rootStartObserved
			if (-not $processStartTraceMeasured) {
				if ([string]::IsNullOrWhiteSpace($processStartTraceReason)) {
					$processStartTraceReason = "The reconciled Job Object trace did not observe the launched root process."
				}
				$notMeasured.Add("run $run chromium_process_start_latch: $processStartTraceReason")
			}
			$latchedWebEngineStarts = @(
				if ($rootStartObserved) {
					Get-LatchedWebEngineProcessStarts -RootProcessId $process.Id -ProcessStartRecords $processStartRecords
				}
			)
			$idleProcessMetrics = $connectedIdle.process_metrics
			$idleChromiumCount = if ($null -ne $idleProcessMetrics) { [int]$idleProcessMetrics.chromium_process_count } else { 0 }
			$chromiumChildren = [Math]::Max($idleChromiumCount, [int]$finalProcessMetrics.chromium_process_count)
			$runMeasurement = [ordered]@{
				run = $run
				frozen_profile = $runProfile
				startup_to_interactive_ms = [Math]::Round($startupInteractiveMilliseconds, 2)
				startup_to_window_diagnostic_ms = [Math]::Round($startupWindowDiagnosticMilliseconds, 2)
				interactive_state = $interactiveState
				connected_idle_working_set_bytes = if ($connectedIdle.measured) { [int64]$idleProcessMetrics.working_set_bytes } else { $null }
				connected_idle = $connectedIdle
				process_count = if ($connectedIdle.measured) { [int]$idleProcessMetrics.process_count } else { [int]$finalProcessMetrics.process_count }
				process_name_counts = if ($connectedIdle.measured) { $idleProcessMetrics.process_name_counts } else { $finalProcessMetrics.process_name_counts }
				processes_at_connected_idle = if ($connectedIdle.measured) { $idleProcessMetrics.processes } else { @() }
				pre_media_final_process_metrics = $finalProcessMetrics
				chromium_process_count_before_media = $chromiumChildren
				chromium_process_start_latch = [ordered]@{
					measured = $processStartTraceMeasured
					reason = $processStartTraceReason
					mechanism = [string]$processStartTrace.mechanism
					poll_interval_ms = [int]$processStartTrace.poll_interval_ms
					primary_error = [string]$processStartTrace.primary_error
					accounting_reconciled = $null -ne $processStartSnapshot -and [bool]$processStartSnapshot.accounting_reconciled
					job_total_processes = if ($null -ne $processStartSnapshot) { [int]$processStartSnapshot.job_total_processes } else { $null }
					job_active_processes_after_termination = if ($null -ne $processStartSnapshot) { [int]$processStartSnapshot.job_active_processes_after_termination } else { $null }
					all_processes_exited = $null -ne $processStartSnapshot -and [bool]$processStartSnapshot.all_processes_exited
					termination_wait_ms = if ($null -ne $processStartSnapshot) { [int64]$processStartSnapshot.termination_wait_ms } else { $null }
					termination_timed_out = $null -eq $processStartSnapshot -or [bool]$processStartSnapshot.termination_timed_out
					notification_count = if ($null -ne $processStartSnapshot) { [int]$processStartSnapshot.notification_count } else { $processStartRecords.Count }
					unresolved_process_count = if ($null -ne $processStartSnapshot) { [int]$processStartSnapshot.unresolved_process_count } else { $null }
					root_start_observed = $rootStartObserved
					observed_process_start_count = $processStartRecords.Count
					latched_qtwebengine_count = $latchedWebEngineStarts.Count
					latched_qtwebengine_starts = $latchedWebEngineStarts
				}
				pre_media_state = [ordered]@{ measured = $preMediaStateMeasured; reason = $preMediaStateReason }
				room_switches = if ($roomSwitchMeasured) { $RoomSwitchIterations } else { 0 }
				room_switch_workload = [ordered]@{ measured = $roomSwitchMeasured; reason = $roomSwitchReason }
				room_warmup = $roomWarmup
				chat_scroll_workload = $chatScroll
				chat_seed_workload = $chatSeed
				talk_state_workload = $talkState
				performance_phases = [ordered]@{ room_switch = $roomPerformance; chat_seed = $chatSeedPerformance; chat_scroll = $chatScrollPerformance; talk_state = $talkPerformance }
				performance = $performance
			}
			$measurements += $runMeasurement
		} finally {
			if (-not $processStartTraceStopped) {
				try { Stop-ProcessStartTrace -Trace $processStartTrace | Out-Null } catch { }
			}
			if ($null -ne $process -and -not $process.HasExited) {
				try { Set-QmlPerformanceWindowTopmost -Process $process -Topmost $false } catch { }
				Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
				$process.WaitForExit(5000) | Out-Null
			}
			try {
				$profileDirectory = Split-Path -Parent $runProfile.database_path
				$profileFiles = @($runProfile.config_path, $runProfile.database_path)
				$profileFiles += @(Get-ChildItem -LiteralPath $profileDirectory -File -ErrorAction Stop |
					Where-Object { $_.Name -eq 'Console.txt' -or $_.FullName -like "$($runProfile.database_path)-*" } |
					ForEach-Object { $_.FullName })
				$fileQuiescence = Wait-FrozenProfileFilesReady -Paths $profileFiles -TimeoutMilliseconds 10000
				$runProfile["file_quiescence"] = $fileQuiescence
				if (-not [bool]$fileQuiescence.ready) { throw [string]$fileQuiescence.reason }
				$postConfigHash = (Get-FileHash -LiteralPath $runProfile.config_path -Algorithm SHA256).Hash.ToLowerInvariant()
				$postDatabaseHash = (Get-FileHash -LiteralPath $runProfile.database_path -Algorithm SHA256).Hash.ToLowerInvariant()
				$postCompanions = @(Get-ChildItem -LiteralPath (Split-Path -Parent $runProfile.database_path) -File |
					Where-Object { $_.FullName -like "$($runProfile.database_path)-*" } |
					ForEach-Object {
						[ordered]@{ path = $_.FullName; length_bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
					})
				$runProfile["post_run_config_sha256"] = $postConfigHash
				$runProfile["post_run_database_sha256"] = $postDatabaseHash
				$runProfile["config_mutated_during_run"] = $postConfigHash -ne [string]$runProfile.config_seed_sha256
				$runProfile["database_mutated_during_run"] = $postDatabaseHash -ne [string]$runProfile.database_seed_sha256 -or $postCompanions.Count -gt 0
				$runProfile["post_run_database_companions"] = $postCompanions
				$runProfile["post_run_hashes_measured"] = $true
		} catch {
			if ($null -eq $runProfile.file_quiescence) {
				$runProfile["file_quiescence"] = [ordered]@{ ready = $false; reason = $_.Exception.Message }
			}
				$runProfile["post_run_hashes_measured"] = $false
				$runProfile["post_run_hash_reason"] = $_.Exception.Message
				$notMeasured.Add("run $run frozen_profile_post_hash: $($_.Exception.Message)")
			}
		}
	}
	$sourceProfileCheck = Test-FrozenProfileSourceUnchanged -Seed $frozenProfileSeed
	if (-not [bool]$sourceProfileCheck.unchanged) {
		$notMeasured.Add("frozen_profile_source: $($sourceProfileCheck.reason)")
	}
} finally {
	$env:MUMBLE_CHAT_PERF_TRACE = $savedEnvironment.MUMBLE_CHAT_PERF_TRACE
	$env:MUMBLE_CHAT_PERF_TRACE_PATH = $savedEnvironment.MUMBLE_CHAT_PERF_TRACE_PATH
	$env:MUMBLE_MODERN_AUTOMATION_PORT = $savedEnvironment.MUMBLE_MODERN_AUTOMATION_PORT
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $savedEnvironment.MUMBLE_MODERN_AUTOMATION_TOKEN
	try {
		$cleanupResult = Remove-IsolatedPerformanceStateRoot -RootPath $isolatedStateRoot `
			-AllowedParentPath $outputDirectory -TimeoutMilliseconds 10000
		$isolationCleanup.removed = [bool]$cleanupResult.removed
		$isolationCleanup.reason = [string]$cleanupResult.reason
		$isolationCleanup["waited_ms"] = [int64]$cleanupResult.waited_ms
		$isolationCleanup["attempts"] = [int]$cleanupResult.attempts
		if (-not [bool]$cleanupResult.removed) {
			$notMeasured.Add("frozen_profile_cleanup: $([string]$cleanupResult.reason)")
		}
	} catch {
		$isolationCleanup.reason = $_.Exception.Message
		$notMeasured.Add("frozen_profile_cleanup: $($_.Exception.Message)")
	}
}

$executableSha256After = $null
try {
	$executableSha256After = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
} catch {
	$notMeasured.Add("candidate_executable: unable to hash the measured executable after the five runs: $($_.Exception.Message)")
}
$executableUnchangedDuringRuns = -not [string]::IsNullOrWhiteSpace($executableSha256After) -and
	$executableSha256After -ceq $executableSha256
if (-not $executableUnchangedDuringRuns -and -not [string]::IsNullOrWhiteSpace($executableSha256After)) {
	$notMeasured.Add("candidate_executable: SHA-256 changed during the five-run measurement")
}

$startupValues = @($measurements | ForEach-Object { [double]$_.startup_to_interactive_ms })
$startupWindowDiagnosticValues = @($measurements | ForEach-Object { [double]$_.startup_to_window_diagnostic_ms })
$memoryValues = @($measurements | Where-Object { [bool]$_.connected_idle.measured } |
	ForEach-Object { [double]$_.connected_idle_working_set_bytes })

# Frame-time acceptance applies independently to room switching, chat scrolling,
# and talk-state churn. Flattening every phase into one percentile list could let
# a fast phase hide a slow one, while looking only at room switching would leave
# the two most animation-heavy workloads ungated. Aggregate five-run medians per
# phase, then gate the worst phase median.
$requiredFramePhases = @("room_switch", "chat_scroll", "talk_state")
$requiredInputSamplesByPhase = [ordered]@{
	room_switch = $MinimumInputSamples
	chat_scroll = $requiredChatScrollInputSamples
	talk_state = $TalkStateTransitions
}
$framePhaseSummaries = [ordered]@{}
foreach ($phaseName in $requiredFramePhases) {
	$phaseMeasurements = @($measurements | ForEach-Object { $_.performance_phases.$phaseName } |
		Where-Object { $null -ne $_ })
	$p95Values = @($phaseMeasurements | ForEach-Object { [double]$_.p95FrameMs })
	$p99Values = @($phaseMeasurements | ForEach-Object { [double]$_.p99FrameMs })
	$sampleCounts = @($phaseMeasurements | ForEach-Object { [int]$_.frameSampleCount })
	$stallCounts = @($phaseMeasurements | ForEach-Object { [int]$_.uiStallCount })
	$inputMeasurements = @($phaseMeasurements | Where-Object { [int]$_.inputSampleCount -gt 0 })
	$inputP95Values = @($inputMeasurements | ForEach-Object { [double]$_.p95InputLatencyMs })
	$inputSampleCounts = @($phaseMeasurements | ForEach-Object { [int]$_.inputSampleCount })
	$resetAnalyses = @($phaseMeasurements | ForEach-Object {
		Get-QmlModelResetAnalysis -Performance $_ -RequiredCounters $requiredModelResetCounters
	})
	$measuredResetAnalyses = @($resetAnalyses | Where-Object { $_.measured })
	$aggregateResetCounts = [ordered]@{}
	foreach ($counter in $requiredModelResetCounters) { $aggregateResetCounts[$counter] = 0L }
	foreach ($analysis in $measuredResetAnalyses) {
		foreach ($counter in $requiredModelResetCounters) {
			$aggregateResetCounts[$counter] += [int64]$analysis.counts[$counter]
		}
	}
	$totalModelResets = 0L
	foreach ($count in $aggregateResetCounts.Values) { $totalModelResets += [int64]$count }
	$syncUiAnalyses = @($phaseMeasurements | ForEach-Object { Get-QmlSyncUiOperationAnalysis -Performance $_ })
	$measuredSyncUiAnalyses = @($syncUiAnalyses | Where-Object { $_.measured })
	$aggregateSyncUiCounts = [ordered]@{ network = 0L; plugin = 0L; file = 0L }
	foreach ($analysis in $measuredSyncUiAnalyses) {
		foreach ($counter in @("network", "plugin", "file")) {
			$aggregateSyncUiCounts[$counter] += [int64]$analysis.counts[$counter]
		}
	}
	$totalSyncUiViolations = 0L
	foreach ($count in $aggregateSyncUiCounts.Values) { $totalSyncUiViolations += [int64]$count }
	$framePhaseSummaries[$phaseName] = [ordered]@{
		run_count = $phaseMeasurements.Count
		median_p95_ms = Get-Percentile -Values $p95Values -Percentile 50
		median_p99_ms = Get-Percentile -Values $p99Values -Percentile 50
		worst_p95_ms = if ($p95Values.Count -gt 0) { ($p95Values | Measure-Object -Maximum).Maximum } else { $null }
		worst_p99_ms = if ($p99Values.Count -gt 0) { ($p99Values | Measure-Object -Maximum).Maximum } else { $null }
		minimum_frame_sample_count = if ($sampleCounts.Count -gt 0) { ($sampleCounts | Measure-Object -Minimum).Minimum } else { 0 }
		total_ui_stalls_over_50_ms = if ($stallCounts.Count -gt 0) { ($stallCounts | Measure-Object -Sum).Sum } else { 0 }
		input_run_count = $inputMeasurements.Count
		required_input_samples_per_run = [int]$requiredInputSamplesByPhase[$phaseName]
		minimum_input_sample_count = if ($inputSampleCounts.Count -gt 0) { ($inputSampleCounts | Measure-Object -Minimum).Minimum } else { 0 }
		median_input_p95_ms = Get-Percentile -Values $inputP95Values -Percentile 50
		worst_input_p95_ms = if ($inputP95Values.Count -gt 0) { ($inputP95Values | Measure-Object -Maximum).Maximum } else { $null }
		model_reset_counter_run_count = $measuredResetAnalyses.Count
		model_reset_counts = $aggregateResetCounts
		total_model_reset_count = $totalModelResets
		model_reset_reasons = @($resetAnalyses | Where-Object { -not $_.measured } | ForEach-Object { $_.reason } | Select-Object -Unique)
		sync_ui_operation_run_count = $measuredSyncUiAnalyses.Count
		sync_ui_operation_passed_run_count = @($measuredSyncUiAnalyses | Where-Object { $_.passed }).Count
		sync_ui_operation_violation_counts = $aggregateSyncUiCounts
		total_sync_ui_operation_violation_count = $totalSyncUiViolations
		sync_ui_operation_reasons = @($syncUiAnalyses | Where-Object { -not $_.measured } | ForEach-Object { $_.reason } | Select-Object -Unique)
	}
}

$idleSyncUiAnalyses = @($measurements | ForEach-Object {
	Get-QmlSyncUiOperationAnalysis -Performance $_.connected_idle.performance
})
$measuredIdleSyncUiAnalyses = @($idleSyncUiAnalyses | Where-Object { $_.measured })
$idleSyncUiCounts = [ordered]@{ network = 0L; plugin = 0L; file = 0L }
foreach ($analysis in $measuredIdleSyncUiAnalyses) {
	foreach ($counter in @("network", "plugin", "file")) {
		$idleSyncUiCounts[$counter] += [int64]$analysis.counts[$counter]
	}
}
$idleSyncUiTotal = 0L
foreach ($count in $idleSyncUiCounts.Values) { $idleSyncUiTotal += [int64]$count }
$idleSyncUiSummary = [ordered]@{
	run_count = $measuredIdleSyncUiAnalyses.Count
	passed_run_count = @($measuredIdleSyncUiAnalyses | Where-Object { $_.passed }).Count
	violation_counts = $idleSyncUiCounts
	total_violation_count = $idleSyncUiTotal
	reasons = @($idleSyncUiAnalyses | Where-Object { -not $_.measured } | ForEach-Object { $_.reason } | Select-Object -Unique)
}
$idleUiStallCount = @($measurements | Where-Object { $null -ne $_.connected_idle.performance } |
	ForEach-Object { [int]$_.connected_idle.performance.uiStallCount } | Measure-Object -Sum).Sum

$phaseMedianP95Values = @($framePhaseSummaries.Values | ForEach-Object { $_.median_p95_ms } |
	Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$phaseMedianP99Values = @($framePhaseSummaries.Values | ForEach-Object { $_.median_p99_ms } |
	Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$phaseWorstP95Values = @($framePhaseSummaries.Values | ForEach-Object { $_.worst_p95_ms } |
	Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$phaseWorstP99Values = @($framePhaseSummaries.Values | ForEach-Object { $_.worst_p99_ms } |
	Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$phaseMinimumSampleCounts = @($framePhaseSummaries.Values | ForEach-Object { [int]$_.minimum_frame_sample_count })
$phaseStallCounts = @($framePhaseSummaries.Values | ForEach-Object { [int]$_.total_ui_stalls_over_50_ms })
$phaseMedianInputValues = @($framePhaseSummaries.Values | ForEach-Object { $_.median_input_p95_ms } |
	Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$phaseWorstInputValues = @($framePhaseSummaries.Values | ForEach-Object { $_.worst_input_p95_ms } |
	Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$phaseMinimumInputCounts = @($framePhaseSummaries.Values | ForEach-Object { [int]$_.minimum_input_sample_count })
$phaseModelResetTotals = @($framePhaseSummaries.Values | ForEach-Object { [int64]$_.total_model_reset_count })
$syncUiViolationCounts = [ordered]@{
	network = [int64]$idleSyncUiCounts.network
	plugin = [int64]$idleSyncUiCounts.plugin
	file = [int64]$idleSyncUiCounts.file
}
foreach ($phaseName in $requiredFramePhases) {
	foreach ($counter in @("network", "plugin", "file")) {
		$syncUiViolationCounts[$counter] += [int64]$framePhaseSummaries[$phaseName].sync_ui_operation_violation_counts[$counter]
	}
}
$totalSyncUiOperationViolations = 0L
foreach ($count in $syncUiViolationCounts.Values) { $totalSyncUiOperationViolations += [int64]$count }
$connectedIdleMeasuredRuns = @($measurements | Where-Object { [bool]$_.connected_idle.measured }).Count
$processStartLatchMeasuredRuns = @($measurements | Where-Object { [bool]$_.chromium_process_start_latch.measured }).Count
$maxLatchedWebEngineStarts = ($measurements.chromium_process_start_latch.latched_qtwebengine_count | Measure-Object -Maximum).Maximum
$profileHashMeasuredRuns = @($measurements | Where-Object { [bool]$_.frozen_profile.post_run_hashes_measured }).Count
$profileSeedIds = @($measurements.frozen_profile.profile_seed_sha256 | Select-Object -Unique)
$profileConfigPaths = @($measurements.frozen_profile.config_path | Select-Object -Unique)
$profileDatabasePaths = @($measurements.frozen_profile.database_path | Select-Object -Unique)
$profileDatabaseSeedHashes = @($measurements.frozen_profile.database_seed_sha256 | Select-Object -Unique)
$frozenProfileIsolationMeasured = $measurements.Count -eq 5 -and $profileHashMeasuredRuns -eq 5 -and
	$profileSeedIds.Count -eq 1 -and $profileSeedIds[0] -eq [string]$frozenProfileSeed.profile_seed_sha256 -and
	$profileConfigPaths.Count -eq 5 -and $profileDatabasePaths.Count -eq 5 -and
	$profileDatabaseSeedHashes.Count -eq 1 -and
	$profileDatabaseSeedHashes[0] -eq [string]$frozenProfileSeed.source_database_sha256

$summary = [ordered]@{
	runs = $Runs
	startup_to_interactive_median_ms = Get-Percentile -Values $startupValues -Percentile 50
	startup_to_interactive_p95_ms = Get-Percentile -Values $startupValues -Percentile 95
	connected_idle_working_set_median_bytes = Get-Percentile -Values $memoryValues -Percentile 50
	connected_idle_measured_runs = $connectedIdleMeasuredRuns
	max_process_count = ($measurements.process_count | Measure-Object -Maximum).Maximum
	max_chromium_process_count_before_media = ($measurements.chromium_process_count_before_media | Measure-Object -Maximum).Maximum
	max_latched_qtwebengine_process_starts_before_media = $maxLatchedWebEngineStarts
	process_start_latch_measured_runs = $processStartLatchMeasuredRuns
	frozen_profile_hash_measured_runs = $profileHashMeasuredRuns
	worst_frame_p95_ms = ($phaseWorstP95Values | Measure-Object -Maximum).Maximum
	worst_frame_p99_ms = ($phaseWorstP99Values | Measure-Object -Maximum).Maximum
	worst_input_to_visual_p95_ms = if ($phaseWorstInputValues.Count -gt 0) { ($phaseWorstInputValues | Measure-Object -Maximum).Maximum } else { $null }
	# These are the slowest of the independently aggregated five-run phase medians.
	median_frame_p95_ms = ($phaseMedianP95Values | Measure-Object -Maximum).Maximum
	median_frame_p99_ms = ($phaseMedianP99Values | Measure-Object -Maximum).Maximum
	worst_phase_median_input_to_visual_p95_ms = if ($phaseMedianInputValues.Count -gt 0) { ($phaseMedianInputValues | Measure-Object -Maximum).Maximum } else { $null }
	total_ui_stalls_over_50_ms = ($phaseStallCounts | Measure-Object -Sum).Sum + $idleUiStallCount
	minimum_frame_sample_count = ($phaseMinimumSampleCounts | Measure-Object -Minimum).Minimum
	minimum_input_sample_count = ($phaseMinimumInputCounts | Measure-Object -Minimum).Minimum
	total_model_reset_count = ($phaseModelResetTotals | Measure-Object -Sum).Sum
	total_sync_ui_operation_violation_count = $totalSyncUiOperationViolations
	sync_ui_operation_violation_counts = $syncUiViolationCounts
	connected_idle_sync_ui_operations = $idleSyncUiSummary
	connected_idle_ui_stalls_over_50_ms = $idleUiStallCount
	frame_phases = $framePhaseSummaries
	input_phases = $framePhaseSummaries
	diagnostics = [ordered]@{
		startup_to_window_median_ms = Get-Percentile -Values $startupWindowDiagnosticValues -Percentile 50
		startup_to_window_p95_ms = Get-Percentile -Values $startupWindowDiagnosticValues -Percentile 95
		legacy_median_input_to_visual_p95_ms = if ($phaseMedianInputValues.Count -gt 0) { ($phaseMedianInputValues | Measure-Object -Maximum).Maximum } else { $null }
	}
}

foreach ($measurement in $measurements) {
	if (-not [bool]$measurement.room_warmup.measured) {
		$notMeasured.Add("run $($measurement.run) room_warmup: $($measurement.room_warmup.reason)")
	}
	if (-not [bool]$measurement.chat_seed_workload.measured) {
		$notMeasured.Add("run $($measurement.run) chat_seed: $($measurement.chat_seed_workload.reason)")
	}
	if (-not [bool]$measurement.chat_scroll_workload.measured) {
		$notMeasured.Add("run $($measurement.run) chat_scroll: $($measurement.chat_scroll_workload.reason)")
	}
	if (-not [bool]$measurement.talk_state_workload.measured) {
		$notMeasured.Add("run $($measurement.run) talk_state: $($measurement.talk_state_workload.reason)")
	}
}
if ($summary.minimum_frame_sample_count -lt $MinimumFrameSamples) {
	$notMeasured.Add("frame_time: minimum sample count $($summary.minimum_frame_sample_count) is below $MinimumFrameSamples")
}
foreach ($phaseName in $requiredFramePhases) {
	$phaseSummary = $framePhaseSummaries[$phaseName]
	if ($phaseSummary.run_count -ne $Runs) {
		$notMeasured.Add("$phaseName frame_time: measured $($phaseSummary.run_count) of $Runs runs")
	}
	if ($phaseSummary.minimum_frame_sample_count -lt $MinimumFrameSamples) {
		$notMeasured.Add("$phaseName frame_time: minimum sample count $($phaseSummary.minimum_frame_sample_count) is below $MinimumFrameSamples")
	}
	if ($phaseSummary.input_run_count -ne $Runs -or
		$phaseSummary.minimum_input_sample_count -lt $phaseSummary.required_input_samples_per_run) {
		$notMeasured.Add("$phaseName input_to_visual: requires $($phaseSummary.required_input_samples_per_run) samples in each of five runs; measured runs=$($phaseSummary.input_run_count), minimum=$($phaseSummary.minimum_input_sample_count)")
	}
	if ($phaseSummary.model_reset_counter_run_count -ne $Runs) {
		$notMeasured.Add("$phaseName model_resets: all counters were measured in $($phaseSummary.model_reset_counter_run_count) of five runs; $($phaseSummary.model_reset_reasons -join '; ')")
	}
	if ($phaseSummary.sync_ui_operation_run_count -ne $Runs) {
		$notMeasured.Add("$phaseName sync_ui_operations: exact network/plugin/file counters and noSyncUiOperationsPassed were measured in $($phaseSummary.sync_ui_operation_run_count) of five runs; $($phaseSummary.sync_ui_operation_reasons -join '; ')")
	}
}
if ($idleSyncUiSummary.run_count -ne $Runs) {
	$notMeasured.Add("connected_idle sync_ui_operations: exact network/plugin/file counters and noSyncUiOperationsPassed were measured in $($idleSyncUiSummary.run_count) of five runs; $($idleSyncUiSummary.reasons -join '; ')")
}
if (-not $frozenProfileIsolationMeasured) {
	$notMeasured.Add("frozen_profile_isolation: five distinct per-run config/database clones from one verified source snapshot were not proven")
}
$syncUiOperationCountersMeasured = $idleSyncUiSummary.run_count -eq 5 -and
	@($requiredFramePhases | Where-Object { $framePhaseSummaries[$_].sync_ui_operation_run_count -ne 5 }).Count -eq 0

$gates = [ordered]@{
	exactly_five_runs_measured = $Runs -eq 5 -and $measurements.Count -eq 5
	executable_unchanged_during_runs = $executableUnchangedDuringRuns
	frozen_profile_isolation_measured = $frozenProfileIsolationMeasured
	frozen_profile_source_unchanged = $null -ne $sourceProfileCheck -and [bool]$sourceProfileCheck.unchanged
	startup_to_interactive_measured = $startupValues.Count -eq 5
	connected_idle_measured_before_workloads = $connectedIdleMeasuredRuns -eq 5
	chromium_process_start_latch_measured = $processStartLatchMeasuredRuns -eq 5
	no_chromium_before_media = $processStartLatchMeasuredRuns -eq 5 -and
		@($measurements | Where-Object { -not $_.pre_media_state.measured }).Count -eq 0 -and
		$summary.max_chromium_process_count_before_media -eq 0 -and
		$summary.max_latched_qtwebengine_process_starts_before_media -eq 0
	# The locked performance contract uses the median of five independent runs.
	# Worst-run values remain in the report as diagnostics, but do not replace the
	# specified median aggregation with an undocumented stricter gate.
	frame_time_p95_at_most_16_7_ms = $summary.minimum_frame_sample_count -ge $MinimumFrameSamples -and $summary.median_frame_p95_ms -le 16.7
	frame_time_p99_at_most_33_3_ms = $summary.minimum_frame_sample_count -ge $MinimumFrameSamples -and $summary.median_frame_p99_ms -le 33.3
	input_to_visual_room_switch_measured = $framePhaseSummaries.room_switch.input_run_count -eq 5 -and
		$framePhaseSummaries.room_switch.minimum_input_sample_count -ge $framePhaseSummaries.room_switch.required_input_samples_per_run
	input_to_visual_chat_scroll_measured = $framePhaseSummaries.chat_scroll.input_run_count -eq 5 -and
		$framePhaseSummaries.chat_scroll.minimum_input_sample_count -ge $framePhaseSummaries.chat_scroll.required_input_samples_per_run
	input_to_visual_talk_state_measured = $framePhaseSummaries.talk_state.input_run_count -eq 5 -and
		$framePhaseSummaries.talk_state.minimum_input_sample_count -ge $framePhaseSummaries.talk_state.required_input_samples_per_run
	input_to_visual_p95_at_most_50_ms = @($requiredFramePhases | Where-Object {
		$framePhaseSummaries[$_].input_run_count -ne 5 -or
		$framePhaseSummaries[$_].minimum_input_sample_count -lt $framePhaseSummaries[$_].required_input_samples_per_run
	}).Count -eq 0 -and $null -ne $summary.worst_phase_median_input_to_visual_p95_ms -and
		$summary.worst_phase_median_input_to_visual_p95_ms -le 50.0
	no_ui_stalls_over_50_ms = $summary.total_ui_stalls_over_50_ms -eq 0
	model_reset_counters_measured = @($requiredFramePhases | Where-Object {
		$framePhaseSummaries[$_].model_reset_counter_run_count -ne 5
	}).Count -eq 0
	no_room_switch_model_resets = $framePhaseSummaries.room_switch.model_reset_counter_run_count -eq 5 -and
		$framePhaseSummaries.room_switch.total_model_reset_count -eq 0
	no_chat_scroll_model_resets = $framePhaseSummaries.chat_scroll.model_reset_counter_run_count -eq 5 -and
		$framePhaseSummaries.chat_scroll.total_model_reset_count -eq 0
	no_talk_state_model_resets = $framePhaseSummaries.talk_state.model_reset_counter_run_count -eq 5 -and
		$framePhaseSummaries.talk_state.total_model_reset_count -eq 0
	sync_ui_operation_counters_measured = $syncUiOperationCountersMeasured
	no_connected_idle_sync_ui_operations = $idleSyncUiSummary.run_count -eq 5 -and
		$idleSyncUiSummary.passed_run_count -eq 5 -and $idleSyncUiSummary.total_violation_count -eq 0
	no_room_switch_sync_ui_operations = $framePhaseSummaries.room_switch.sync_ui_operation_run_count -eq 5 -and
		$framePhaseSummaries.room_switch.sync_ui_operation_passed_run_count -eq 5 -and
		$framePhaseSummaries.room_switch.total_sync_ui_operation_violation_count -eq 0
	no_chat_scroll_sync_ui_operations = $framePhaseSummaries.chat_scroll.sync_ui_operation_run_count -eq 5 -and
		$framePhaseSummaries.chat_scroll.sync_ui_operation_passed_run_count -eq 5 -and
		$framePhaseSummaries.chat_scroll.total_sync_ui_operation_violation_count -eq 0
	no_talk_state_sync_ui_operations = $framePhaseSummaries.talk_state.sync_ui_operation_run_count -eq 5 -and
		$framePhaseSummaries.talk_state.sync_ui_operation_passed_run_count -eq 5 -and
		$framePhaseSummaries.talk_state.total_sync_ui_operation_violation_count -eq 0
	no_sync_ui_network_operations = $syncUiOperationCountersMeasured -and $syncUiViolationCounts.network -eq 0
	no_sync_ui_plugin_operations = $syncUiOperationCountersMeasured -and $syncUiViolationCounts.plugin -eq 0
	no_sync_ui_file_operations = $syncUiOperationCountersMeasured -and $syncUiViolationCounts.file -eq 0
	room_warmup_measured = @($measurements | Where-Object { -not $_.room_warmup.measured }).Count -eq 0
	room_switch_workload_measured = @($measurements | Where-Object { -not $_.room_switch_workload.measured }).Count -eq 0
	chat_seed_workload_measured = @($measurements | Where-Object { -not $_.chat_seed_workload.measured }).Count -eq 0
	chat_scroll_workload_measured = @($measurements | Where-Object { -not $_.chat_scroll_workload.measured }).Count -eq 0
	talk_state_workload_measured = @($measurements | Where-Object { -not $_.talk_state_workload.measured }).Count -eq 0
	no_legacy_full_snapshot_trace = $null
	no_steady_state_full_bootstrap_trace = $null
	no_model_reset_trace = $null
	no_observed_trace_block_over_50_ms = $null
	startup_20_percent_faster_than_web = $null
	idle_memory_25_percent_lower_than_web = $null
}

$chatPerf = $null
if ($traceFile -and (Test-Path -LiteralPath $traceFile)) {
	$traceLines = @(Get-Content -LiteralPath $traceFile)
	$traceAnalysis = Get-ChatPerfTraceAnalysis -TraceLines $traceLines
	$maxObservedMilliseconds = $traceAnalysis.max_observed_timing_ms
	$chatPerf = [ordered]@{
		path = $traceFile
		line_count = $traceLines.Count
		max_observed_timing_ms = $maxObservedMilliseconds
		legacy_full_snapshot_line_count = $traceAnalysis.legacy_full_snapshot_line_count
		steady_state_full_bootstrap_line_count = $traceAnalysis.steady_state_full_bootstrap_line_count
		steady_state_full_bootstrap_total = $traceAnalysis.steady_state_full_bootstrap_total
		model_reset_line_count = $traceAnalysis.model_reset_line_count
		model_reset_total = $traceAnalysis.model_reset_total
		model_reset_counts = $traceAnalysis.model_reset_counts
	}
	if ($traceLines.Count -eq 0 -or $null -eq $maxObservedMilliseconds) {
		$notMeasured.Add("chat_perf_trace: trace contained no timing samples")
	} else {
		$gates.no_legacy_full_snapshot_trace = $traceAnalysis.legacy_full_snapshot_line_count -eq 0
		$gates.no_steady_state_full_bootstrap_trace =
			$traceAnalysis.steady_state_full_bootstrap_line_count -eq 0 -and
			$traceAnalysis.steady_state_full_bootstrap_total -eq 0
		$gates.no_model_reset_trace = $traceAnalysis.model_reset_total -eq 0
		$gates.no_observed_trace_block_over_50_ms = $maxObservedMilliseconds -le 50.0
	}
} elseif ($traceFile) {
	$notMeasured.Add("chat_perf_trace: requested trace file was not created")
}

if ($connectedIdleMeasuredRuns -eq 5) {
	$gates.startup_20_percent_faster_than_web =
		$summary.startup_to_interactive_median_ms -le ($baselineStartupMedian * 0.8)
	$gates.idle_memory_25_percent_lower_than_web =
		$summary.connected_idle_working_set_median_bytes -le ($baselineIdleWorkingSetMedian * 0.75)
}

$result = [ordered]@{
	schema_version = $performanceSchemaVersion
	contract_id = $performanceContractId
	measured_at_utc = [DateTime]::UtcNow.ToString("o")
	candidate_id = $resolvedCandidateId
	source_commit = if ([string]::IsNullOrWhiteSpace($resolvedSourceCommit)) { $null } else { $resolvedSourceCommit }
	executable_sha256 = $executableSha256
	executable_sha256_after = $executableSha256After
	machine_fingerprint_sha256 = $machineFingerprint
	profile_seed_sha256 = [string]$frozenProfileSeed.profile_seed_sha256
	executable = $executablePath
	source_config = $configFilePath
	frozen_profile_provenance = [ordered]@{
		profile_seed_sha256 = [string]$frozenProfileSeed.profile_seed_sha256
		source_config_path = [string]$frozenProfileSeed.source_config_path
		source_config_sha256 = [string]$frozenProfileSeed.source_config_sha256
		source_database_path = [string]$frozenProfileSeed.source_database_path
		source_database_sha256 = [string]$frozenProfileSeed.source_database_sha256
		source_database_length_bytes = [int64]$frozenProfileSeed.source_database_length_bytes
		source_integrity_after_runs = $sourceProfileCheck
		isolated_state_cleanup = $isolationCleanup
	}
	measurement_scope = "exactly five frozen-profile runs; cold startup to windowReady+mainCaptureReady; connected/quiescent no-media idle before warmup; room-switch, chat-scroll, and talk-state phases"
	sync_ui_operation_detection_scope = $syncUiOperationDetectionScope
	thresholds = [ordered]@{
		frame_p95_ms = 16.7
		frame_p99_ms = 33.3
		input_to_visual_p95_ms = 50.0
		ui_stall_ms = 50.0
		room_switch_input_samples_per_run = $MinimumInputSamples
		chat_scroll_input_samples_per_run = $requiredChatScrollInputSamples
		talk_state_input_samples_per_run = $TalkStateTransitions
	}
	measurements = $measurements
	summary = $summary
	gates = $gates
	chat_perf_trace = $chatPerf
	baseline = $baselineDiagnostics
	not_measured = @($notMeasured)
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputFile -Encoding utf8
$result | ConvertTo-Json -Depth 12

$failedGates = @($gates.GetEnumerator() | Where-Object { $_.Value -ne $true } | ForEach-Object { $_.Key })
if ($notMeasured.Count -gt 0 -or $failedGates.Count -gt 0) {
	if ($notMeasured.Count -gt 0) { Write-Error "QML performance not measured: $($notMeasured -join '; ')" -ErrorAction Continue }
	if ($failedGates.Count -gt 0) { Write-Error "QML performance gates failed or were not measured: $($failedGates -join ', ')" -ErrorAction Continue }
	exit 1
}
