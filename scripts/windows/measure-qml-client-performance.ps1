[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Executable,

	[Parameter(Mandatory = $true)]
	[string]$ConfigPath,

	[int]$Runs = 5,
	[int]$StartupTimeoutSeconds = 20,
	[int]$IdleSeconds = 5,
	[string]$OutputPath = ".tmp\qml-performance.json",
	[string]$WebBaselinePath = "",
	[string]$ChatPerfTracePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($Runs -lt 1) { throw "Runs must be at least one." }
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$configFilePath = (Resolve-Path -LiteralPath $ConfigPath).Path

function Get-Percentile {
	param([double[]]$Values, [double]$Percentile)
	if ($Values.Count -eq 0) { return $null }
	$sorted = @($Values | Sort-Object)
	$index = [Math]::Ceiling(($Percentile / 100.0) * $sorted.Count) - 1
	return [double]$sorted[[Math]::Max(0, [Math]::Min($sorted.Count - 1, $index))]
}

function Get-ProcessTreeIds {
	param([int]$RootProcessId)
	$processes = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name)
	$ids = [System.Collections.Generic.HashSet[int]]::new()
	$null = $ids.Add($RootProcessId)
	do {
		$added = $false
		foreach ($process in $processes) {
			if ($ids.Contains([int]$process.ParentProcessId) -and $ids.Add([int]$process.ProcessId)) {
				$added = $true
			}
		}
	} while ($added)
	return @($processes | Where-Object { $ids.Contains([int]$_.ProcessId) })
}

$previousTraceEnabled = $env:MUMBLE_CHAT_PERF_TRACE
$previousTracePath = $env:MUMBLE_CHAT_PERF_TRACE_PATH
if (-not [string]::IsNullOrWhiteSpace($ChatPerfTracePath)) {
	$traceFile = [IO.Path]::GetFullPath($ChatPerfTracePath)
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $traceFile) | Out-Null
	Remove-Item -LiteralPath $traceFile -Force -ErrorAction SilentlyContinue
	$env:MUMBLE_CHAT_PERF_TRACE = "1"
	$env:MUMBLE_CHAT_PERF_TRACE_PATH = $traceFile
}

$measurements = @()
for ($run = 1; $run -le $Runs; ++$run) {
	$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
	$process = Start-Process -FilePath $executablePath -ArgumentList @("--multiple", "--config", $configFilePath) -PassThru
	try {
		$deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
		do {
			Start-Sleep -Milliseconds 25
			$process.Refresh()
			if ($process.HasExited) { throw "Mumble exited during startup with code $($process.ExitCode)." }
		} while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

		if ($process.MainWindowHandle -eq 0) { throw "Timed out waiting for the QML top-level window." }
		$startupMilliseconds = $stopwatch.Elapsed.TotalMilliseconds
		Start-Sleep -Seconds $IdleSeconds

		$tree = @(Get-ProcessTreeIds -RootProcessId $process.Id)
		$workingSetBytes = 0L
		foreach ($entry in $tree) {
			$live = Get-Process -Id ([int]$entry.ProcessId) -ErrorAction SilentlyContinue
			if ($live) { $workingSetBytes += [int64]$live.WorkingSet64 }
		}
		$chromiumChildren = @($tree | Where-Object { $_.Name -ieq "QtWebEngineProcess.exe" }).Count
		$measurements += [ordered]@{
			run = $run
			startup_to_window_ms = [Math]::Round($startupMilliseconds, 2)
			idle_working_set_bytes = $workingSetBytes
			process_count = $tree.Count
			chromium_process_count = $chromiumChildren
		}
	} finally {
		if (-not $process.HasExited) {
			Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
			$process.WaitForExit(5000) | Out-Null
		}
	}
}

$env:MUMBLE_CHAT_PERF_TRACE = $previousTraceEnabled
$env:MUMBLE_CHAT_PERF_TRACE_PATH = $previousTracePath

$startupValues = @($measurements | ForEach-Object { [double]$_.startup_to_window_ms })
$memoryValues = @($measurements | ForEach-Object { [double]$_.idle_working_set_bytes })
$summary = [ordered]@{
	runs = $Runs
	startup_to_window_median_ms = Get-Percentile -Values $startupValues -Percentile 50
	startup_to_window_p95_ms = Get-Percentile -Values $startupValues -Percentile 95
	idle_working_set_median_bytes = Get-Percentile -Values $memoryValues -Percentile 50
	max_process_count = ($measurements.process_count | Measure-Object -Maximum).Maximum
	max_chromium_process_count = ($measurements.chromium_process_count | Measure-Object -Maximum).Maximum
}

$gates = [ordered]@{
	no_chromium_before_media = $summary.max_chromium_process_count -eq 0
	no_legacy_full_snapshot_trace = $null
	no_observed_trace_block_over_50_ms = $null
	startup_20_percent_faster_than_web = $null
	idle_memory_25_percent_lower_than_web = $null
}

$chatPerf = $null
if (-not [string]::IsNullOrWhiteSpace($ChatPerfTracePath) -and (Test-Path -LiteralPath $traceFile)) {
	$traceLines = @(Get-Content -LiteralPath $traceFile)
	$maxDurations = @($traceLines | ForEach-Object {
		if ($_ -match '\bmax_ms=(?<duration>[0-9.]+)') { [double]$Matches.duration }
	})
	$maxObservedMilliseconds = if ($maxDurations.Count -gt 0) {
		($maxDurations | Measure-Object -Maximum).Maximum
	} else { 0.0 }
	$legacySnapshotLines = @($traceLines | Where-Object { $_ -match '(?i)full[._ -]?snapshot|buildModernShellSnapshot' })
	$chatPerf = [ordered]@{
		path = $traceFile
		line_count = $traceLines.Count
		max_observed_timing_ms = $maxObservedMilliseconds
		legacy_full_snapshot_line_count = $legacySnapshotLines.Count
	}
	$gates.no_legacy_full_snapshot_trace = $legacySnapshotLines.Count -eq 0
	$gates.no_observed_trace_block_over_50_ms = $maxObservedMilliseconds -le 50.0
}
if (-not [string]::IsNullOrWhiteSpace($WebBaselinePath)) {
	$baseline = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $WebBaselinePath).Path | ConvertFrom-Json
	$baselineSummary = if ($baseline.PSObject.Properties.Name -contains "summary") { $baseline.summary } else { $baseline }
	$gates.startup_20_percent_faster_than_web =
		$summary.startup_to_window_median_ms -le ([double]$baselineSummary.startup_to_window_median_ms * 0.8)
	$gates.idle_memory_25_percent_lower_than_web =
		$summary.idle_working_set_median_bytes -le ([double]$baselineSummary.idle_working_set_median_bytes * 0.75)
}

$result = [ordered]@{
	measured_at_utc = [DateTime]::UtcNow.ToString("o")
	executable = $executablePath
	config = $configFilePath
	measurement_scope = "startup-to-top-level-window and post-delay process-tree working set; OS file cache is not flushed"
	measurements = $measurements
	summary = $summary
	gates = $gates
	chat_perf_trace = $chatPerf
	not_measured = @("connected_idle", "frame_time_p95", "frame_time_p99", "input_to_visual_p95", "ui_thread_blocking")
}

$outputFile = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputFile) | Out-Null
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outputFile -Encoding utf8
$result | ConvertTo-Json -Depth 6

$gateFailed = (-not $gates.no_chromium_before_media) -or
	($gates.no_legacy_full_snapshot_trace -eq $false) -or
	($gates.no_observed_trace_block_over_50_ms -eq $false) -or
	($gates.startup_20_percent_faster_than_web -eq $false) -or
	($gates.idle_memory_25_percent_lower_than_web -eq $false)
if ($gateFailed) {
	exit 1
}
