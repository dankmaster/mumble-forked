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
	[int]$MinimumFrameSamples = 30,
	[int]$MinimumInputSamples = 10,
	[int]$AutomationPort = 0,
	[string]$AutomationToken = "",
	[string]$OutputPath = ".tmp\qml-performance.json",
	[string]$WebBaselinePath = "",
	[string]$ChatPerfTracePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($Runs -lt 1) { throw "Runs must be at least one." }
if ($RoomSwitchIterations -lt 2) { throw "RoomSwitchIterations must be at least two." }
if ($MinimumFrameSamples -lt 1) { throw "MinimumFrameSamples must be at least one." }
if ($MinimumInputSamples -lt 1) { throw "MinimumInputSamples must be at least one." }
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$configFilePath = (Resolve-Path -LiteralPath $ConfigPath).Path

function Get-Percentile {
	param([double[]]$Values, [double]$Percentile)
	if ($Values.Count -eq 0) { return $null }
	$sorted = @($Values | Sort-Object)
	$index = [Math]::Ceiling(($Percentile / 100.0) * $sorted.Count) - 1
	return [double]$sorted[[Math]::Max(0, [Math]::Min($sorted.Count - 1, $index))]
}

function Get-FreeTcpPort {
	$listener = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
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

function Get-QmlSnapshot {
	param([int]$Port, [string]$Token)
	return (Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "snapshot" }).snapshot
}

function Wait-ConnectedRoomState {
	param([int]$Port, [string]$Token, [int]$TimeoutSeconds)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do {
		$snapshot = Get-QmlSnapshot -Port $Port -Token $Token
		$rooms = @($snapshot.voiceRooms) + @($snapshot.textRooms)
		$tokens = @($rooms | ForEach-Object { [string]$_.token } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
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
		$snapshot = Get-QmlSnapshot -Port $Port -Token $Token
		if ([string]$snapshot.selection.scopeToken -eq $ScopeToken -or [string]$snapshot.activeScope.scopeToken -eq $ScopeToken) {
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
		$mark = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
			command = "qmlPerformanceMarkInput"; operationId = $operationId
		}
		$measuredId = [string]$mark.operationId
		if ([string]::IsNullOrWhiteSpace($measuredId)) { throw "qmlPerformanceMarkInput returned no operation ID." }
		$select = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
			command = "selectScope"; scopeToken = $scopeToken; async = $false
		}
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

function Test-OptionalAutomationWorkload {
	param([int]$Port, [string]$Token, [string]$Command)
	try {
		$response = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = $Command; async = $false }
		if (-not ($response.PSObject.Properties.Name -contains "frameSampleDelta") -or [int]$response.frameSampleDelta -le 0) {
			throw "Automation workload '$Command' returned no rendered frame sample delta."
		}
		return [pscustomobject]@{ measured = $true; response = $response; reason = $null }
	} catch {
		return [pscustomobject]@{ measured = $false; response = $null; reason = $_.Exception.Message }
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
		$runPort = if ($AutomationPort -gt 0) { $AutomationPort } else { Get-FreeTcpPort }
		$runToken = if ([string]::IsNullOrWhiteSpace($AutomationToken)) { [Guid]::NewGuid().ToString("N") } else { $AutomationToken }
		$env:MUMBLE_MODERN_AUTOMATION_PORT = [string]$runPort
		$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $runToken

		$stopwatch = [Diagnostics.Stopwatch]::StartNew()
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
			Wait-QmlAutomation -Port $runPort -Token $runToken -TimeoutSeconds $StartupTimeoutSeconds | Out-Null

			$ready = Wait-ConnectedRoomState -Port $runPort -Token $runToken -TimeoutSeconds $WorkloadReadyTimeoutSeconds
			$roomSwitchMeasured = $false
			$roomSwitchReason = $null
			if ($null -eq $ready) {
				$roomSwitchReason = "connected=true and at least two room scopes were not observable"
				$notMeasured.Add("run $run room_switch: $roomSwitchReason")
			}

			Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
			Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
			if ($null -ne $ready) {
				try {
					Invoke-RoomSwitchWorkload -Port $runPort -Token $runToken -ScopeTokens $ready.tokens -Iterations $RoomSwitchIterations
					$roomSwitchMeasured = $true
				} catch {
					$roomSwitchReason = $_.Exception.Message
					$notMeasured.Add("run $run room_switch: $roomSwitchReason")
				}
			}

			$chatScroll = Test-OptionalAutomationWorkload -Port $runPort -Token $runToken -Command "qmlPerformanceChatScrollWorkload"
			$talkState = Test-OptionalAutomationWorkload -Port $runPort -Token $runToken -Command "qmlPerformanceTalkStateWorkload"
			Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
			$performance = (Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{
				command = "qmlPerformanceSnapshot"
			}).performance

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
				chromium_process_count_before_media = $chromiumChildren
				room_switches = if ($roomSwitchMeasured) { $RoomSwitchIterations } else { 0 }
				room_switch_workload = [ordered]@{ measured = $roomSwitchMeasured; reason = $roomSwitchReason }
				chat_scroll_workload = $chatScroll
				talk_state_workload = $talkState
				performance = $performance
			}
		} finally {
			if (-not $process.HasExited) {
				Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
				$process.WaitForExit(5000) | Out-Null
			}
		}
	}
} finally {
	$env:MUMBLE_CHAT_PERF_TRACE = $savedEnvironment.MUMBLE_CHAT_PERF_TRACE
	$env:MUMBLE_CHAT_PERF_TRACE_PATH = $savedEnvironment.MUMBLE_CHAT_PERF_TRACE_PATH
	$env:MUMBLE_MODERN_AUTOMATION_PORT = $savedEnvironment.MUMBLE_MODERN_AUTOMATION_PORT
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $savedEnvironment.MUMBLE_MODERN_AUTOMATION_TOKEN
}

$startupValues = @($measurements | ForEach-Object { [double]$_.startup_to_window_ms })
$memoryValues = @($measurements | ForEach-Object { [double]$_.idle_working_set_bytes })
$frameP95Values = @($measurements | ForEach-Object { [double]$_.performance.p95FrameMs })
$frameP99Values = @($measurements | ForEach-Object { [double]$_.performance.p99FrameMs })
$inputP95Values = @($measurements | ForEach-Object { [double]$_.performance.p95InputLatencyMs })
$stallCounts = @($measurements | ForEach-Object { [int]$_.performance.uiStallCount })
$frameSampleCounts = @($measurements | ForEach-Object { [int]$_.performance.frameSampleCount })
$inputSampleCounts = @($measurements | ForEach-Object { [int]$_.performance.inputSampleCount })

$summary = [ordered]@{
	runs = $Runs
	startup_to_window_median_ms = Get-Percentile -Values $startupValues -Percentile 50
	startup_to_window_p95_ms = Get-Percentile -Values $startupValues -Percentile 95
	idle_working_set_median_bytes = Get-Percentile -Values $memoryValues -Percentile 50
	max_process_count = ($measurements.process_count | Measure-Object -Maximum).Maximum
	max_chromium_process_count_before_media = ($measurements.chromium_process_count_before_media | Measure-Object -Maximum).Maximum
	worst_frame_p95_ms = ($frameP95Values | Measure-Object -Maximum).Maximum
	worst_frame_p99_ms = ($frameP99Values | Measure-Object -Maximum).Maximum
	worst_input_to_visual_p95_ms = ($inputP95Values | Measure-Object -Maximum).Maximum
	total_ui_stalls_over_50_ms = ($stallCounts | Measure-Object -Sum).Sum
	minimum_frame_sample_count = ($frameSampleCounts | Measure-Object -Minimum).Minimum
	minimum_input_sample_count = ($inputSampleCounts | Measure-Object -Minimum).Minimum
}

foreach ($measurement in $measurements) {
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
if ($summary.minimum_input_sample_count -lt $MinimumInputSamples) {
	$notMeasured.Add("input_to_visual: minimum sample count $($summary.minimum_input_sample_count) is below $MinimumInputSamples")
}

$gates = [ordered]@{
	no_chromium_before_media = $summary.max_chromium_process_count_before_media -eq 0
	frame_time_p95_at_most_16_7_ms = $summary.minimum_frame_sample_count -ge $MinimumFrameSamples -and $summary.worst_frame_p95_ms -le 16.7
	frame_time_p99_at_most_33_3_ms = $summary.minimum_frame_sample_count -ge $MinimumFrameSamples -and $summary.worst_frame_p99_ms -le 33.3
	input_to_visual_p95_at_most_50_ms = $summary.minimum_input_sample_count -ge $MinimumInputSamples -and $summary.worst_input_to_visual_p95_ms -le 50.0
	no_ui_stalls_over_50_ms = $summary.total_ui_stalls_over_50_ms -eq 0
	room_switch_workload_measured = @($measurements | Where-Object { -not $_.room_switch_workload.measured }).Count -eq 0
	chat_scroll_workload_measured = @($measurements | Where-Object { -not $_.chat_scroll_workload.measured }).Count -eq 0
	talk_state_workload_measured = @($measurements | Where-Object { -not $_.talk_state_workload.measured }).Count -eq 0
	no_legacy_full_snapshot_trace = $null
	no_observed_trace_block_over_50_ms = $null
	startup_20_percent_faster_than_web = $null
	idle_memory_25_percent_lower_than_web = $null
}

$chatPerf = $null
if ($traceFile -and (Test-Path -LiteralPath $traceFile)) {
	$traceLines = @(Get-Content -LiteralPath $traceFile)
	$maxDurations = @($traceLines | ForEach-Object {
		if ($_ -match '\bmax_ms=(?<duration>[0-9.]+)') { [double]$Matches.duration }
	})
	$maxObservedMilliseconds = if ($maxDurations.Count -gt 0) { ($maxDurations | Measure-Object -Maximum).Maximum } else { $null }
	$legacySnapshotLines = @($traceLines | Where-Object { $_ -match '(?i)full[._ -]?snapshot|buildModernShellSnapshot' })
	$chatPerf = [ordered]@{
		path = $traceFile
		line_count = $traceLines.Count
		max_observed_timing_ms = $maxObservedMilliseconds
		legacy_full_snapshot_line_count = $legacySnapshotLines.Count
	}
	if ($traceLines.Count -eq 0 -or $null -eq $maxObservedMilliseconds) {
		$notMeasured.Add("chat_perf_trace: trace contained no timing samples")
	} else {
		$gates.no_legacy_full_snapshot_trace = $legacySnapshotLines.Count -eq 0
		$gates.no_observed_trace_block_over_50_ms = $maxObservedMilliseconds -le 50.0
	}
} elseif ($traceFile) {
	$notMeasured.Add("chat_perf_trace: requested trace file was not created")
}

if ([string]::IsNullOrWhiteSpace($WebBaselinePath)) {
	$notMeasured.Add("web_baseline: WebBaselinePath was not provided")
} else {
	$baseline = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $WebBaselinePath).Path | ConvertFrom-Json
	$baselineSummary = if ($baseline.PSObject.Properties.Name -contains "summary") { $baseline.summary } else { $baseline }
	if ($null -eq $baselineSummary.startup_to_window_median_ms -or $null -eq $baselineSummary.idle_working_set_median_bytes) {
		$notMeasured.Add("web_baseline: startup or idle-memory median is missing")
	} else {
		$gates.startup_20_percent_faster_than_web =
			$summary.startup_to_window_median_ms -le ([double]$baselineSummary.startup_to_window_median_ms * 0.8)
		$gates.idle_memory_25_percent_lower_than_web =
			$summary.idle_working_set_median_bytes -le ([double]$baselineSummary.idle_working_set_median_bytes * 0.75)
	}
}

$result = [ordered]@{
	measured_at_utc = [DateTime]::UtcNow.ToString("o")
	executable = $executablePath
	config = $configFilePath
	measurement_scope = "connected QML room-switch workload plus required chat-scroll/talk-state automation; median startup/memory across runs"
	thresholds = [ordered]@{ frame_p95_ms = 16.7; frame_p99_ms = 33.3; input_to_visual_p95_ms = 50.0; ui_stall_ms = 50.0 }
	measurements = $measurements
	summary = $summary
	gates = $gates
	chat_perf_trace = $chatPerf
	not_measured = @($notMeasured)
}

$outputFile = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputFile) | Out-Null
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputFile -Encoding utf8
$result | ConvertTo-Json -Depth 12

$failedGates = @($gates.GetEnumerator() | Where-Object { $_.Value -ne $true } | ForEach-Object { $_.Key })
if ($notMeasured.Count -gt 0 -or $failedGates.Count -gt 0) {
	if ($notMeasured.Count -gt 0) { Write-Error "QML performance not measured: $($notMeasured -join '; ')" -ErrorAction Continue }
	if ($failedGates.Count -gt 0) { Write-Error "QML performance gates failed or were not measured: $($failedGates -join ', ')" -ErrorAction Continue }
	exit 1
}
