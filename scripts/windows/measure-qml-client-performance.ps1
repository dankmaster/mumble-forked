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
if ($TalkStateTransitions -lt 40) { throw "TalkStateTransitions must be at least 40." }
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

	return [pscustomobject]@{
		max_observed_timing_ms = if ($maxDurations.Count -gt 0) { ($maxDurations | Measure-Object -Maximum).Maximum } else { $null }
		legacy_full_snapshot_line_count = $legacySnapshotLines.Count
		steady_state_full_bootstrap_line_count = $steadyStateBootstrapLines.Count
		steady_state_full_bootstrap_total = $steadyStateBootstrapTotal
	}
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
			$roomWarmup = [ordered]@{ measured = $false; scope_count = 0; stable_polls = 0; reason = $null }
			if ($null -eq $ready) {
				$roomSwitchReason = "connected=true and at least two room scopes were not observable"
				$notMeasured.Add("run $run room_switch: $roomSwitchReason")
			} else {
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
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatSeedStart" } | Out-Null
				$seedDeadline = [DateTime]::UtcNow.AddSeconds(5); $seedStatus = $null
				do {
					$seedStatus = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatSeedStatus" }
					if ([bool]$seedStatus.ready) { break }
					Start-Sleep -Milliseconds 10
				} while ([DateTime]::UtcNow -lt $seedDeadline)
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$chatSeedPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
				if (-not [bool]$seedStatus.ready) { throw "Chat seed did not become render-ready: $($seedStatus | ConvertTo-Json -Compress -Depth 8)" }
				$chatSeed = [pscustomobject]@{ measured = $true; response = $seedStatus; reason = $null }

				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				$scrollStart = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatScrollStart" }
				if (-not [bool]$scrollStart.scroll.started) { throw "Chat scroll refused to start: $($scrollStart | ConvertTo-Json -Compress -Depth 8)" }
				$scrollDeadline = [DateTime]::UtcNow.AddSeconds(3); $scrollStatus = $null
				do {
					$scrollStatus = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceChatScrollStatus" }
					if ([bool]$scrollStatus.scroll.moved -and -not [bool]$scrollStatus.scroll.running -and [int]$scrollStatus.performance.frameSampleCount -gt 0) { break }
					Start-Sleep -Milliseconds 10
				} while ([DateTime]::UtcNow -lt $scrollDeadline)
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$chatScrollPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
				if (-not [bool]$scrollStatus.scroll.moved -or [bool]$scrollStatus.scroll.running -or [int]$chatScrollPerformance.frameSampleCount -le 0) { throw "Chat scroll did not complete with a presented frame." }
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
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkStart" } | Out-Null
				for ($transition = 0; $transition -lt $TalkStateTransitions; ++$transition) {
					Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkTransition" } | Out-Null
					Start-Sleep -Milliseconds 20
				}
				$talkDeadline = [DateTime]::UtcNow.AddSeconds(5); $talkStatus = $null
				do {
					$talkStatus = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkStatus" }
					if ([int]$talkStatus.transitionCount -ge $TalkStateTransitions -and
						[int]$talkStatus.presentedFrameDelta -ge $TalkStateTransitions) { break }
					Start-Sleep -Milliseconds 10
				} while ([DateTime]::UtcNow -lt $talkDeadline)
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$talkPerformance = Get-QmlPerformanceSnapshot -Port $runPort -Token $runToken
				if ([int]$talkStatus.transitionCount -lt $TalkStateTransitions) {
					throw "Talk-state workload completed only $($talkStatus.transitionCount) of $TalkStateTransitions transitions."
				}
				if ([int]$talkStatus.presentedFrameDelta -lt $TalkStateTransitions -or
					[int]$talkPerformance.frameSampleCount -lt $TalkStateTransitions) {
					throw "Talk-state workload produced fewer than $TalkStateTransitions presented/frame samples."
				}
				$talkState = [pscustomobject]@{ measured = $true; response = $talkStatus; reason = $null }
			} catch {
				$talkState.reason = $_.Exception.Message
			} finally {
				try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null } catch { }
				try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceTalkFinalize" } | Out-Null } catch { }
			}
			$performance = $roomPerformance

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
				room_warmup = $roomWarmup
				chat_scroll_workload = $chatScroll
				chat_seed_workload = $chatSeed
				talk_state_workload = $talkState
				performance_phases = [ordered]@{ room_switch = $roomPerformance; chat_seed = $chatSeedPerformance; chat_scroll = $chatScrollPerformance; talk_state = $talkPerformance }
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
$inputP95Values = @($measurements | ForEach-Object { [double]$_.performance.p95InputLatencyMs })
$inputSampleCounts = @($measurements | ForEach-Object { [int]$_.performance.inputSampleCount })

# Frame-time acceptance applies independently to room switching, chat scrolling,
# and talk-state churn. Flattening every phase into one percentile list could let
# a fast phase hide a slow one, while looking only at room switching would leave
# the two most animation-heavy workloads ungated. Aggregate five-run medians per
# phase, then gate the worst phase median.
$requiredFramePhases = @("room_switch", "chat_scroll", "talk_state")
$framePhaseSummaries = [ordered]@{}
foreach ($phaseName in $requiredFramePhases) {
	$phaseMeasurements = @($measurements | ForEach-Object { $_.performance_phases.$phaseName } |
		Where-Object { $null -ne $_ })
	$p95Values = @($phaseMeasurements | ForEach-Object { [double]$_.p95FrameMs })
	$p99Values = @($phaseMeasurements | ForEach-Object { [double]$_.p99FrameMs })
	$sampleCounts = @($phaseMeasurements | ForEach-Object { [int]$_.frameSampleCount })
	$stallCounts = @($phaseMeasurements | ForEach-Object { [int]$_.uiStallCount })
	$framePhaseSummaries[$phaseName] = [ordered]@{
		run_count = $phaseMeasurements.Count
		median_p95_ms = Get-Percentile -Values $p95Values -Percentile 50
		median_p99_ms = Get-Percentile -Values $p99Values -Percentile 50
		worst_p95_ms = if ($p95Values.Count -gt 0) { ($p95Values | Measure-Object -Maximum).Maximum } else { $null }
		worst_p99_ms = if ($p99Values.Count -gt 0) { ($p99Values | Measure-Object -Maximum).Maximum } else { $null }
		minimum_frame_sample_count = if ($sampleCounts.Count -gt 0) { ($sampleCounts | Measure-Object -Minimum).Minimum } else { 0 }
		total_ui_stalls_over_50_ms = if ($stallCounts.Count -gt 0) { ($stallCounts | Measure-Object -Sum).Sum } else { 0 }
	}
}

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

$summary = [ordered]@{
	runs = $Runs
	startup_to_window_median_ms = Get-Percentile -Values $startupValues -Percentile 50
	startup_to_window_p95_ms = Get-Percentile -Values $startupValues -Percentile 95
	idle_working_set_median_bytes = Get-Percentile -Values $memoryValues -Percentile 50
	max_process_count = ($measurements.process_count | Measure-Object -Maximum).Maximum
	max_chromium_process_count_before_media = ($measurements.chromium_process_count_before_media | Measure-Object -Maximum).Maximum
	worst_frame_p95_ms = ($phaseWorstP95Values | Measure-Object -Maximum).Maximum
	worst_frame_p99_ms = ($phaseWorstP99Values | Measure-Object -Maximum).Maximum
	worst_input_to_visual_p95_ms = ($inputP95Values | Measure-Object -Maximum).Maximum
	# These are the slowest of the independently aggregated five-run phase medians.
	median_frame_p95_ms = ($phaseMedianP95Values | Measure-Object -Maximum).Maximum
	median_frame_p99_ms = ($phaseMedianP99Values | Measure-Object -Maximum).Maximum
	median_input_to_visual_p95_ms = Get-Percentile -Values $inputP95Values -Percentile 50
	total_ui_stalls_over_50_ms = ($phaseStallCounts | Measure-Object -Sum).Sum
	minimum_frame_sample_count = ($phaseMinimumSampleCounts | Measure-Object -Minimum).Minimum
	minimum_input_sample_count = ($inputSampleCounts | Measure-Object -Minimum).Minimum
	frame_phases = $framePhaseSummaries
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
}
if ($summary.minimum_input_sample_count -lt $MinimumInputSamples) {
	$notMeasured.Add("input_to_visual: minimum sample count $($summary.minimum_input_sample_count) is below $MinimumInputSamples")
}

$gates = [ordered]@{
	no_chromium_before_media = $summary.max_chromium_process_count_before_media -eq 0
	# The locked performance contract uses the median of five independent runs.
	# Worst-run values remain in the report as diagnostics, but do not replace the
	# specified median aggregation with an undocumented stricter gate.
	frame_time_p95_at_most_16_7_ms = $summary.minimum_frame_sample_count -ge $MinimumFrameSamples -and $summary.median_frame_p95_ms -le 16.7
	frame_time_p99_at_most_33_3_ms = $summary.minimum_frame_sample_count -ge $MinimumFrameSamples -and $summary.median_frame_p99_ms -le 33.3
	input_to_visual_p95_at_most_50_ms = $summary.minimum_input_sample_count -ge $MinimumInputSamples -and $summary.median_input_to_visual_p95_ms -le 50.0
	no_ui_stalls_over_50_ms = $summary.total_ui_stalls_over_50_ms -eq 0
	room_warmup_measured = @($measurements | Where-Object { -not $_.room_warmup.measured }).Count -eq 0
	room_switch_workload_measured = @($measurements | Where-Object { -not $_.room_switch_workload.measured }).Count -eq 0
	chat_seed_workload_measured = @($measurements | Where-Object { -not $_.chat_seed_workload.measured }).Count -eq 0
	chat_scroll_workload_measured = @($measurements | Where-Object { -not $_.chat_scroll_workload.measured }).Count -eq 0
	talk_state_workload_measured = @($measurements | Where-Object { -not $_.talk_state_workload.measured }).Count -eq 0
	no_legacy_full_snapshot_trace = $null
	no_steady_state_full_bootstrap_trace = $null
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
	}
	if ($traceLines.Count -eq 0 -or $null -eq $maxObservedMilliseconds) {
		$notMeasured.Add("chat_perf_trace: trace contained no timing samples")
	} else {
		$gates.no_legacy_full_snapshot_trace = $traceAnalysis.legacy_full_snapshot_line_count -eq 0
		$gates.no_steady_state_full_bootstrap_trace =
			$traceAnalysis.steady_state_full_bootstrap_line_count -eq 0 -and
			$traceAnalysis.steady_state_full_bootstrap_total -eq 0
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
