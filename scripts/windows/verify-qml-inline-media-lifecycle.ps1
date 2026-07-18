[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Executable,

	[Parameter(Mandatory = $true)]
	[string]$ConfigPath,

	[int]$Runs = 5,
	[int]$CyclesPerRun = 2,
	[int]$AutomationPort = 0,
	[string]$AutomationToken = "",
	[int]$StartupTimeoutSeconds = 30,
	[int]$ActivationTimeoutSeconds = 15,
	[int]$CloseTimeoutSeconds = 10,
	[int]$PollIntervalMilliseconds = 5,
	[string]$OutputPath = ".tmp\qml-inline-media-lifecycle.json",
	[switch]$KeepIsolatedState
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($Runs -ne 5) { throw "Runs must be exactly five for the inline-media lifecycle contract." }
if ($CyclesPerRun -lt 2) { throw "CyclesPerRun must be at least two." }
if ($AutomationPort -lt 0 -or $AutomationPort -gt 65535) { throw "AutomationPort must be zero or a valid TCP port." }
if ($StartupTimeoutSeconds -lt 1) { throw "StartupTimeoutSeconds must be positive." }
if ($ActivationTimeoutSeconds -lt 1) { throw "ActivationTimeoutSeconds must be positive." }
if ($CloseTimeoutSeconds -lt 1) { throw "CloseTimeoutSeconds must be positive." }
if ($PollIntervalMilliseconds -lt 1 -or $PollIntervalMilliseconds -gt 100) {
	throw "PollIntervalMilliseconds must be between 1 and 100 ms."
}

$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$sourceConfigPath = (Resolve-Path -LiteralPath $ConfigPath).Path
$outputFilePath = [IO.Path]::GetFullPath($OutputPath)
$contractId = "windows-qml-inline-native-media-lifecycle-v2"
$schemaVersion = 1
$firstActivationTargetMilliseconds = 50.0
$tinyLocalWav = "data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQIAAAAAAA=="

function Get-ObjectPropertyValue {
	param(
		[AllowNull()][object]$Object,
		[Parameter(Mandatory = $true)][string]$Name,
		[AllowNull()][object]$Default = $null
	)
	if ($null -eq $Object) { return $Default }
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property) { return $Default }
	return $property.Value
}

function Set-ObjectPropertyValue {
	param(
		[Parameter(Mandatory = $true)][object]$Object,
		[Parameter(Mandatory = $true)][string]$Name,
		[AllowNull()][object]$Value
	)
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property) {
		$Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
	} else {
		$property.Value = $Value
	}
}

function Get-OrAddObjectProperty {
	param(
		[Parameter(Mandatory = $true)][object]$Object,
		[Parameter(Mandatory = $true)][string]$Name
	)
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property -or $null -eq $property.Value) {
		$value = [pscustomobject]@{}
		Set-ObjectPropertyValue -Object $Object -Name $Name -Value $value
		return $value
	}
	return $property.Value
}

function New-IsolatedConfig {
	param(
		[Parameter(Mandatory = $true)][string]$SourcePath,
		[Parameter(Mandatory = $true)][string]$StateDirectory
	)
	New-Item -ItemType Directory -Force -Path $StateDirectory | Out-Null
	$config = Get-Content -Raw -LiteralPath $SourcePath | ConvertFrom-Json
	if ($null -eq $config) { throw "The source config did not contain a JSON object." }

	$network = Get-OrAddObjectProperty -Object $config -Name "network"
	Set-ObjectPropertyValue -Object $network -Name "auto_connect_to_last_server" -Value $false

	$update = Get-OrAddObjectProperty -Object $config -Name "update"
	Set-ObjectPropertyValue -Object $update -Name "check_for_updates" -Value $false
	Set-ObjectPropertyValue -Object $update -Name "check_for_plugin_updates" -Value $false
	Set-ObjectPropertyValue -Object $update -Name "auto_update_plugins" -Value $false

	$misc = Get-OrAddObjectProperty -Object $config -Name "misc"
	$databasePath = (Join-Path $StateDirectory "mumble.sqlite") -replace "\\", "/"
	Set-ObjectPropertyValue -Object $misc -Name "database_location" -Value $databasePath

	$ui = Get-OrAddObjectProperty -Object $config -Name "ui"
	Set-ObjectPropertyValue -Object $ui -Name "quit_behavior" -Value "AlwaysQuit"
	Set-ObjectPropertyValue -Object $config -Name "mumble_has_quit_normally" -Value $true

	$plugins = Get-ObjectPropertyValue -Object $config -Name "plugins"
	if ($null -ne $plugins) {
		foreach ($pluginProperty in @($plugins.PSObject.Properties)) {
			$plugin = $pluginProperty.Value
			if ($null -eq $plugin) { continue }
			Set-ObjectPropertyValue -Object $plugin -Name "enabled" -Value $false
			Set-ObjectPropertyValue -Object $plugin -Name "positional_data_enabled" -Value $false
		}
	}

	$isolatedPath = Join-Path $StateDirectory "mumble_settings.json"
	$json = $config | ConvertTo-Json -Depth 100
	[IO.File]::WriteAllText($isolatedPath, $json, [Text.UTF8Encoding]::new($false))
	return $isolatedPath
}

function Get-FreeTcpPort {
	$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
}

function Invoke-QmlAutomationCommand {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][hashtable]$Request,
		[int]$TimeoutMilliseconds = 8000
	)
	$payload = @{}
	foreach ($key in $Request.Keys) { $payload[$key] = $Request[$key] }
	$payload["token"] = $Token
	$json = $payload | ConvertTo-Json -Depth 20 -Compress
	$client = [Net.Sockets.TcpClient]::new()
	$pending = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
	if (-not $pending.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
		$client.Dispose()
		throw "Timed out connecting to QML automation on port $Port."
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
			$writer.WriteLine($json)
			$writer.Flush()
			$line = $reader.ReadLine()
			if ([string]::IsNullOrWhiteSpace($line)) { throw "QML automation returned an empty response." }
			$response = $line | ConvertFrom-Json
			if (-not [bool](Get-ObjectPropertyValue -Object $response -Name "ok" -Default $false)) {
				$errorText = [string](Get-ObjectPropertyValue -Object $response -Name "error" -Default "unknown error")
				throw "QML automation command '$($Request.command)' failed: $errorText"
			}
			return $response
		} finally {
			$reader.Dispose()
			$writer.Dispose()
		}
	} finally {
		$client.Dispose()
	}
}

function Wait-QmlAutomation {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][Diagnostics.Process]$Process,
		[Parameter(Mandatory = $true)][int]$TimeoutSeconds
	)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$lastError = $null
	do {
		$Process.Refresh()
		if ($Process.HasExited) { throw "Mumble exited during startup with code $($Process.ExitCode)." }
		try {
			return Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "ping" } -TimeoutMilliseconds 1000
		} catch {
			$lastError = $_.Exception.Message
			Start-Sleep -Milliseconds 25
		}
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for QML automation. Last error: $lastError"
}

function Wait-QmlInteractiveShell {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][Diagnostics.Process]$Process,
		[Parameter(Mandatory = $true)][int]$TimeoutSeconds
	)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$lastState = $null
	do {
		$Process.Refresh()
		if ($Process.HasExited) { throw "Mumble exited before the QML shell became interactive." }
		$response = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "qmlReadinessState" }
		$lastState = $response
		if ([bool](Get-ObjectPropertyValue -Object $response -Name "windowReady" -Default $false) -and
			[bool](Get-ObjectPropertyValue -Object $response -Name "mainCaptureReady" -Default $false)) {
			return $response
		}
		Start-Sleep -Milliseconds 10
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for the interactive QML shell. Last state: $($lastState | ConvertTo-Json -Depth 8 -Compress)"
}

function Initialize-InlineMediaCardFixture {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token
	)
	$response = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{
		command = "setQmlVisualGateState"
		case_id = "inline-media-lifecycle"
		width = 1280
		height = 900
		theme = "dark"
		layout = "regular"
		density = "comfortable"
		state = "connected"
		motd_variant = "none"
		rich_preview_variant = "direct-media"
		rich_preview_size = "default"
		presentation_family = "media"
		case_variant = "direct-media"
		surface_variant = "none"
	}
	$applied = Get-ObjectPropertyValue -Object $response -Name "applied"
	$messageId = [string](Get-ObjectPropertyValue -Object $applied -Name "rich_preview_message_id" -Default "")
	if ([string]::IsNullOrWhiteSpace($messageId) -or
		-not [bool](Get-ObjectPropertyValue -Object $applied -Name "rich_preview_present" -Default $false) -or
		[bool](Get-ObjectPropertyValue -Object $applied -Name "surface_present" -Default $true)) {
		throw "The deterministic rich-card fixture did not expose an inactive inline-media owner: $($applied | ConvertTo-Json -Depth 8 -Compress)"
	}
	return [pscustomobject]@{ message_id = $messageId; applied = $applied }
}

function Initialize-ProcessPollingMonitorType {
	if ($null -ne ('MumbleInlineMediaProcessPollingMonitor' -as [type])) { return }
	Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;

public sealed class MumbleInlineMediaProcessPollingMonitor : IDisposable {
    private const uint TH32CS_SNAPPROCESS = 0x00000002;
    private static readonly IntPtr InvalidHandleValue = new IntPtr(-1);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct PROCESSENTRY32 {
        public uint dwSize;
        public uint cntUsage;
        public uint th32ProcessID;
        public IntPtr th32DefaultHeapID;
        public uint th32ModuleID;
        public uint cntThreads;
        public uint th32ParentProcessID;
        public int pcPriClassBase;
        public uint dwFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string szExeFile;
    }

    public sealed class Record {
        public long Sequence { get; set; }
        public int ProcessId { get; set; }
        public int ParentProcessId { get; set; }
        public string ProcessName { get; set; }
        public string ObservedAtUtc { get; set; }
        public bool IsDescendant { get; set; }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint processId);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool Process32FirstW(IntPtr snapshot, ref PROCESSENTRY32 entry);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool Process32NextW(IntPtr snapshot, ref PROCESSENTRY32 entry);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    private readonly object sync = new object();
    private readonly List<Record> pending = new List<Record>();
    private readonly HashSet<int> seen = new HashSet<int>();
    private readonly ManualResetEvent stop = new ManualResetEvent(false);
    private readonly int intervalMilliseconds;
    private Thread thread;
    private int rootProcessId;
    private long sequence;
    private Exception failure;

    public MumbleInlineMediaProcessPollingMonitor(int intervalMilliseconds) {
        if (intervalMilliseconds < 1 || intervalMilliseconds > 100)
            throw new ArgumentOutOfRangeException("intervalMilliseconds");
        this.intervalMilliseconds = intervalMilliseconds;
    }

    public void Start() {
        if (thread != null) throw new InvalidOperationException("The monitor was already started.");
        thread = new Thread(Run);
        thread.IsBackground = true;
        thread.Name = "Mumble inline-media process latch";
        thread.Start();
    }

    public void SetRootProcessId(int processId) {
        if (processId <= 0) throw new ArgumentOutOfRangeException("processId");
        Volatile.Write(ref rootProcessId, processId);
    }

    public Record[] Drain() {
        lock (sync) {
            ThrowIfFailed();
            Record[] result = pending.ToArray();
            pending.Clear();
            return result;
        }
    }

    public void Stop() {
        stop.Set();
        Thread current = thread;
        if (current != null && !current.Join(3000))
            throw new TimeoutException("The process polling monitor did not stop.");
        lock (sync) ThrowIfFailed();
    }

    public void Dispose() {
        try { Stop(); } finally { stop.Dispose(); }
    }

    private void ThrowIfFailed() {
        if (failure != null) throw new InvalidOperationException("The process polling monitor failed.", failure);
    }

    private void Run() {
        try {
            while (!stop.WaitOne(0)) {
                ObserveSnapshot();
                if (stop.WaitOne(intervalMilliseconds)) break;
            }
        } catch (Exception ex) {
            lock (sync) failure = ex;
        }
    }

    private void ObserveSnapshot() {
        IntPtr snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == InvalidHandleValue)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateToolhelp32Snapshot failed.");
        try {
            var entries = new List<PROCESSENTRY32>();
            var parents = new Dictionary<int, int>();
            PROCESSENTRY32 entry = new PROCESSENTRY32();
            entry.dwSize = (uint)Marshal.SizeOf(typeof(PROCESSENTRY32));
            if (!Process32FirstW(snapshot, ref entry)) {
                int error = Marshal.GetLastWin32Error();
                if (error == 18) return; // ERROR_NO_MORE_FILES
                throw new Win32Exception(error, "Process32FirstW failed.");
            }
            do {
                entries.Add(entry);
                parents[(int)entry.th32ProcessID] = (int)entry.th32ParentProcessID;
                entry.dwSize = (uint)Marshal.SizeOf(typeof(PROCESSENTRY32));
            } while (Process32NextW(snapshot, ref entry));

            int root = Volatile.Read(ref rootProcessId);
            foreach (PROCESSENTRY32 candidate in entries) {
                if (!String.Equals(candidate.szExeFile, "QtWebEngineProcess.exe", StringComparison.OrdinalIgnoreCase))
                    continue;
                int pid = (int)candidate.th32ProcessID;
                lock (sync) {
                    if (!seen.Add(pid)) continue;
                    pending.Add(new Record {
                        Sequence = ++sequence,
                        ProcessId = pid,
                        ParentProcessId = (int)candidate.th32ParentProcessID,
                        ProcessName = candidate.szExeFile,
                        ObservedAtUtc = DateTime.UtcNow.ToString("o"),
                        IsDescendant = root > 0 && IsDescendantOf(pid, root, parents)
                    });
                }
            }
        } finally {
            CloseHandle(snapshot);
        }
    }

    private static bool IsDescendantOf(int processId, int root, Dictionary<int, int> parents) {
        var visited = new HashSet<int>();
        int current = processId;
        while (parents.ContainsKey(current) && visited.Add(current)) {
            int parent = parents[current];
            if (parent == root) return true;
            if (parent <= 0 || parent == current) return false;
            current = parent;
        }
        return false;
    }
}
'@
}

function Start-ProcessStartTrace {
	$sourceIdentifier = "MumbleInlineMediaProcessStart:$([Guid]::NewGuid().ToString('N'))"
	try {
		Register-CimIndicationEvent -Namespace "root/cimv2" -Query "SELECT * FROM Win32_ProcessStartTrace" `
			-SourceIdentifier $sourceIdentifier -ErrorAction Stop | Out-Null
		return [pscustomobject]@{
			source_identifier = $sourceIdentifier
			measured = $true
			mode = "cim-process-start-event"
			monitor = $null
			fallback_reason = $null
		}
	} catch {
		$fallbackReason = $_.Exception.Message
		Unregister-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue
		Remove-Event -SourceIdentifier $sourceIdentifier -ErrorAction SilentlyContinue
		Initialize-ProcessPollingMonitorType
		$monitor = [MumbleInlineMediaProcessPollingMonitor]::new(5)
		$monitor.Start()
		return [pscustomobject]@{
			source_identifier = $null
			measured = $true
			mode = "toolhelp-polling-latch"
			monitor = $monitor
			fallback_reason = $fallbackReason
		}
	}
}

function Set-ProcessStartTraceRoot {
	param(
		[Parameter(Mandatory = $true)]$Trace,
		[Parameter(Mandatory = $true)][int]$RootProcessId
	)
	if ($Trace.mode -eq "toolhelp-polling-latch") {
		$Trace.monitor.SetRootProcessId($RootProcessId)
	}
}

function Receive-ProcessStartTrace {
	param(
		[Parameter(Mandatory = $true)]$Trace,
		[int]$DeliveryGraceMilliseconds = 0
	)
	if ($DeliveryGraceMilliseconds -gt 0) { Start-Sleep -Milliseconds $DeliveryGraceMilliseconds }
	$records = [Collections.Generic.List[object]]::new()
	if ($Trace.mode -eq "toolhelp-polling-latch") {
		foreach ($record in @($Trace.monitor.Drain())) {
			$records.Add([pscustomobject]@{
				process_id = [int]$record.ProcessId
				parent_process_id = [int]$record.ParentProcessId
				process_name = [string]$record.ProcessName
				time_created = [string]$record.ObservedAtUtc
				is_descendant = [bool]$record.IsDescendant
				sequence = [int64]$record.Sequence
			})
		}
		return @($records.ToArray())
	}
	foreach ($eventRecord in @(Get-Event -SourceIdentifier $Trace.source_identifier -ErrorAction SilentlyContinue)) {
		try {
			$instance = $eventRecord.SourceEventArgs.NewEvent
			if ($null -eq $instance) { continue }
			$records.Add([pscustomobject]@{
				process_id = [int]$instance.ProcessID
				parent_process_id = [int]$instance.ParentProcessID
				process_name = [string]$instance.ProcessName
				time_created = [string]$instance.TIME_CREATED
				is_descendant = $null
				sequence = $null
			})
		} finally {
			Remove-Event -EventIdentifier $eventRecord.EventIdentifier -ErrorAction SilentlyContinue
		}
	}
	return @($records.ToArray())
}

function Stop-ProcessStartTrace {
	param([AllowNull()]$Trace)
	if ($null -eq $Trace) { return }
	if ($Trace.mode -eq "toolhelp-polling-latch") {
		$Trace.monitor.Dispose()
		return
	}
	Unregister-Event -SourceIdentifier $Trace.source_identifier -ErrorAction SilentlyContinue
	Remove-Event -SourceIdentifier $Trace.source_identifier -ErrorAction SilentlyContinue
}

function Get-LatchedWebEngineProcessStarts {
	param(
		[Parameter(Mandatory = $true)][int]$RootProcessId,
		[AllowEmptyCollection()][object[]]$ProcessStartRecords
	)
	$parentByPid = @{}
	foreach ($record in $ProcessStartRecords) {
		$parentByPid[[string][int]$record.process_id] = [int]$record.parent_process_id
	}
	$latched = [Collections.Generic.List[object]]::new()
	foreach ($record in @($ProcessStartRecords | Where-Object { $_.process_name -ieq "QtWebEngineProcess.exe" })) {
		$isDescendantProperty = $record.PSObject.Properties["is_descendant"]
		if ($null -ne $isDescendantProperty -and $isDescendantProperty.Value -eq $true) {
			$latched.Add($record)
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
		if ($isDescendant) { $latched.Add($record) }
	}
	return @($latched.ToArray())
}

function Get-ProcessTreeSnapshot {
	param([Parameter(Mandatory = $true)][int]$RootProcessId)
	$all = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name, CommandLine)
	$ids = [Collections.Generic.HashSet[int]]::new()
	$null = $ids.Add($RootProcessId)
	do {
		$added = $false
		foreach ($entry in $all) {
			if ($ids.Contains([int]$entry.ParentProcessId) -and $ids.Add([int]$entry.ProcessId)) { $added = $true }
		}
	} while ($added)
	$tree = @($all | Where-Object { $ids.Contains([int]$_.ProcessId) })
	$webEngine = @($tree | Where-Object { $_.Name -ieq "QtWebEngineProcess.exe" })
	$renderers = @($webEngine | Where-Object { [string]$_.CommandLine -match "(?i)(?:^|\s)--type=renderer(?:\s|$)" })
	return [pscustomobject]@{
		process_count = $tree.Count
		qtwebengine_process_count = $webEngine.Count
		chromium_renderer_process_count = $renderers.Count
		qtwebengine_processes = @($webEngine | ForEach-Object {
			$type = "unknown"
			if ([string]$_.CommandLine -match '(?i)(?:^|\s)--type=(?<type>[^\s"]+)') { $type = $Matches.type }
			[ordered]@{ pid = [int]$_.ProcessId; parent_pid = [int]$_.ParentProcessId; process_type = $type }
		})
	}
}

function Wait-InlineMediaSurface {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][Diagnostics.Stopwatch]$Stopwatch,
		[Parameter(Mandatory = $true)][int]$TimeoutSeconds,
		[Parameter(Mandatory = $true)][int]$PollMilliseconds
	)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$media = $null
	do {
		$response = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "qmlReadinessState" }
		$media = Get-ObjectPropertyValue -Object $response -Name "media"
		$rendererState = [string](Get-ObjectPropertyValue -Object $media -Name "rendererState" -Default "")
		if ($null -ne $media -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "active" -Default $false) -and
			[string](Get-ObjectPropertyValue -Object $media -Name "presentation" -Default "") -eq "inline" -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "rendererPresent" -Default $false) -and
			$rendererState -in @("loading", "active") -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "windowReady" -Default $false)) {
			return [pscustomobject]@{
				surface_activation_latency_ms = [double]$Stopwatch.Elapsed.TotalMilliseconds
				state = $media
			}
		}
		if ($rendererState -in @("error", "component-error")) {
			throw "The inline renderer entered '$rendererState': $($media | ConvertTo-Json -Depth 8 -Compress)"
		}
		Start-Sleep -Milliseconds $PollMilliseconds
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for the active inline renderer surface. Last media state: $($media | ConvertTo-Json -Depth 8 -Compress)"
}

function Wait-InlineRendererReady {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][Diagnostics.Stopwatch]$Stopwatch,
		[Parameter(Mandatory = $true)][int]$TimeoutSeconds,
		[Parameter(Mandatory = $true)][int]$PollMilliseconds
	)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$media = $null
	do {
		$response = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "qmlReadinessState" }
		$media = Get-ObjectPropertyValue -Object $response -Name "media"
		$backendState = [string](Get-ObjectPropertyValue -Object $media -Name "state" -Default "")
		if ($null -ne $media -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "active" -Default $false) -and
			[string](Get-ObjectPropertyValue -Object $media -Name "presentation" -Default "") -eq "inline" -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "rendererPresent" -Default $false) -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "rendererActive" -Default $false) -and
			[bool](Get-ObjectPropertyValue -Object $media -Name "rendererReady" -Default $false) -and
			$backendState -in @("paused", "playing")) {
			return [pscustomobject]@{
				renderer_ready_latency_ms = [double]$Stopwatch.Elapsed.TotalMilliseconds
				playback_ready_latency_ms = [double]$Stopwatch.Elapsed.TotalMilliseconds
				state = $media
			}
		}
		$rendererState = [string](Get-ObjectPropertyValue -Object $media -Name "rendererState" -Default "")
		if ($rendererState -in @("error", "component-error") -or $backendState -eq "error") {
			throw "The inline renderer entered '$rendererState': $($media | ConvertTo-Json -Depth 8 -Compress)"
		}
		Start-Sleep -Milliseconds $PollMilliseconds
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for the inline renderer document to become ready. Last media state: $($media | ConvertTo-Json -Depth 8 -Compress)"
}

function Wait-InlineMediaClosed {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][Diagnostics.Stopwatch]$Stopwatch,
		[Parameter(Mandatory = $true)][int]$TimeoutSeconds,
		[Parameter(Mandatory = $true)][int]$PollMilliseconds
	)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$media = $null
	do {
		$response = Invoke-QmlAutomationCommand -Port $Port -Token $Token -Request @{ command = "qmlReadinessState" }
		$media = Get-ObjectPropertyValue -Object $response -Name "media"
		if ($null -ne $media -and
			-not [bool](Get-ObjectPropertyValue -Object $response -Name "mediaActive" -Default $true) -and
			-not [bool](Get-ObjectPropertyValue -Object $media -Name "active" -Default $true) -and
			-not [bool](Get-ObjectPropertyValue -Object $media -Name "rendererPresent" -Default $true) -and
			-not [bool](Get-ObjectPropertyValue -Object $media -Name "rendererActive" -Default $true) -and
			[string](Get-ObjectPropertyValue -Object $media -Name "presentation" -Default "") -eq "none") {
			return [pscustomobject]@{ close_latency_ms = [double]$Stopwatch.Elapsed.TotalMilliseconds; state = $media }
		}
		Start-Sleep -Milliseconds $PollMilliseconds
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for the inline session and renderer surface to close. Last media state: $($media | ConvertTo-Json -Depth 8 -Compress)"
}

function Get-Median {
	param([AllowEmptyCollection()][double[]]$Values)
	if ($Values.Count -eq 0) { return $null }
	$sorted = @($Values | Sort-Object)
	$middle = [int][Math]::Floor($sorted.Count / 2)
	if (($sorted.Count % 2) -eq 1) { return [double]$sorted[$middle] }
	return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Stop-TestProcess {
	param([AllowNull()][Diagnostics.Process]$Process)
	if ($null -eq $Process) { return }
	try {
		$Process.Refresh()
		if ($Process.HasExited) { return }
		$null = $Process.CloseMainWindow()
		if (-not $Process.WaitForExit(1500)) {
			Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
			$null = $Process.WaitForExit(3000)
		}
	} catch {
		Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
	}
}

$savedEnvironment = @{
	MUMBLE_MODERN_AUTOMATION_PORT = $env:MUMBLE_MODERN_AUTOMATION_PORT
	MUMBLE_MODERN_AUTOMATION_TOKEN = $env:MUMBLE_MODERN_AUTOMATION_TOKEN
}
$stateRoot = Join-Path ([IO.Path]::GetTempPath()) ("mumble-qml-inline-media-" + [Guid]::NewGuid().ToString("N"))
$measurements = [Collections.Generic.List[object]]::new()
$freshProcessLaunchCount = 0

try {
	New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null
	for ($run = 1; $run -le $Runs; ++$run) {
		$runState = Join-Path $stateRoot ("run-{0}" -f $run)
		$isolatedConfig = New-IsolatedConfig -SourcePath $sourceConfigPath -StateDirectory $runState
		$runPort = if ($AutomationPort -gt 0) { $AutomationPort } else { Get-FreeTcpPort }
		$runToken = if ([string]::IsNullOrWhiteSpace($AutomationToken)) { [Guid]::NewGuid().ToString("N") } else { $AutomationToken }
		$env:MUMBLE_MODERN_AUTOMATION_PORT = [string]$runPort
		$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $runToken

		$trace = $null
		$process = $null
		$allTraceRecords = [Collections.Generic.List[object]]::new()
		$cycles = [Collections.Generic.List[object]]::new()
		$preActivation = $null
		$performanceSupported = $false
		$performanceReason = $null
		$performanceSnapshot = $null
		$inlineCardFixture = $null
		$runError = $null
		try {
			$trace = Start-ProcessStartTrace
			$process = Start-Process -FilePath $executablePath `
				-ArgumentList @("--multiple", "--config", ('"{0}"' -f $isolatedConfig)) -PassThru
			++$freshProcessLaunchCount
			Set-ProcessStartTraceRoot -Trace $trace -RootProcessId $process.Id
			Wait-QmlAutomation -Port $runPort -Token $runToken -Process $process -TimeoutSeconds $StartupTimeoutSeconds | Out-Null
			$interactiveState = Wait-QmlInteractiveShell -Port $runPort -Token $runToken -Process $process `
				-TimeoutSeconds $StartupTimeoutSeconds
			if ([bool](Get-ObjectPropertyValue -Object $interactiveState -Name "mediaActive" -Default $true)) {
				throw "The isolated process already had an active media session."
			}
			$inlineCardFixture = Initialize-InlineMediaCardFixture -Port $runPort -Token $runToken

			# The process-start monitor was armed before launch. This short soak and drain
			# happens after injecting the dormant rich card, so it also proves that merely
			# rendering a playable card does not activate Chromium. Even a renderer that
			# starts and exits between live snapshots remains a permanent failure.
			Start-Sleep -Milliseconds 250
			$preState = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlReadinessState" }
			$preRecords = @(Receive-ProcessStartTrace -Trace $trace -DeliveryGraceMilliseconds 100)
			foreach ($record in $preRecords) { $allTraceRecords.Add($record) }
			$preLatched = @(Get-LatchedWebEngineProcessStarts -RootProcessId $process.Id -ProcessStartRecords $preRecords)
			$preTree = Get-ProcessTreeSnapshot -RootProcessId $process.Id
			$preActivation = [pscustomobject]@{
				media_active = [bool](Get-ObjectPropertyValue -Object $preState -Name "mediaActive" -Default $true)
				live_process_tree = $preTree
				latched_qtwebengine_process_start_count = $preLatched.Count
				latched_qtwebengine_process_starts = $preLatched
				zero_qtwebengine_before_activation = -not [bool](Get-ObjectPropertyValue -Object $preState -Name "mediaActive" -Default $true) -and
					$preTree.qtwebengine_process_count -eq 0 -and $preTree.chromium_renderer_process_count -eq 0 -and
					$preLatched.Count -eq 0
			}

			try {
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceReset" } | Out-Null
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceBegin" } | Out-Null
				$performanceSupported = $true
			} catch {
				$performanceReason = $_.Exception.Message
			}

			for ($cycle = 1; $cycle -le $CyclesPerRun; ++$cycle) {
				$surfaceActivation = $null
				$rendererReady = $null
				$activationError = $null
				$rendererReadyError = $null
				$close = $null
				$closeError = $null
				$openResponse = $null
				$cycleRecords = [Collections.Generic.List[object]]::new()
				$treeAfterActivation = $null
				$treeAfterClose = $null
				try {
					if ($performanceSupported) {
						Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{
							command = "qmlPerformanceMarkInput"
							operationId = "inline-media:${run}:$cycle"
						} | Out-Null
					}
					$activationClock = [Diagnostics.Stopwatch]::StartNew()
					$openResponse = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{
						command = "openQmlMediaSession"
						presentation = "inline"
						url = $tinyLocalWav
						mediaMime = "audio/wav"
						audioUrl = ""
						audioMime = ""
						sessionId = $inlineCardFixture.message_id
					}
					$surfaceActivation = Wait-InlineMediaSurface -Port $runPort -Token $runToken -Stopwatch $activationClock `
						-TimeoutSeconds $ActivationTimeoutSeconds -PollMilliseconds $PollIntervalMilliseconds
					try {
						$rendererReady = Wait-InlineRendererReady -Port $runPort -Token $runToken -Stopwatch $activationClock `
							-TimeoutSeconds $ActivationTimeoutSeconds -PollMilliseconds $PollIntervalMilliseconds
					} catch {
						$rendererReadyError = $_.Exception.Message
					}

					# Direct non-adaptive audio/video is a native Qt Multimedia path. Keep
					# the continuous process-start latch armed so even a transient Chromium
					# child fails this gate rather than being missed by a live snapshot.
					foreach ($record in @(Receive-ProcessStartTrace -Trace $trace -DeliveryGraceMilliseconds 50)) {
						$cycleRecords.Add($record)
						$allTraceRecords.Add($record)
					}
					$treeAfterActivation = Get-ProcessTreeSnapshot -RootProcessId $process.Id
				} catch {
					$activationError = $_.Exception.Message
				} finally {
					try {
						$closeClock = [Diagnostics.Stopwatch]::StartNew()
						Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "closeQmlMediaSession" } | Out-Null
						$close = Wait-InlineMediaClosed -Port $runPort -Token $runToken -Stopwatch $closeClock `
							-TimeoutSeconds $CloseTimeoutSeconds -PollMilliseconds $PollIntervalMilliseconds
					} catch {
						$closeError = $_.Exception.Message
					}
					foreach ($record in @(Receive-ProcessStartTrace -Trace $trace -DeliveryGraceMilliseconds 100)) {
						$cycleRecords.Add($record)
						$allTraceRecords.Add($record)
					}
					$treeAfterClose = Get-ProcessTreeSnapshot -RootProcessId $process.Id
				}

				$cycleLatched = @(Get-LatchedWebEngineProcessStarts -RootProcessId $process.Id `
					-ProcessStartRecords @($cycleRecords.ToArray()))
				$rendererProcessObserved = ($null -ne $treeAfterActivation -and $treeAfterActivation.qtwebengine_process_count -gt 0) -or
					$cycleLatched.Count -gt 0
				$rendererBackend = if ($null -ne $rendererReady) {
					[string](Get-ObjectPropertyValue -Object $rendererReady.state -Name "rendererBackend" -Default "")
				} else { "" }
				$nativeBackendObserved = $rendererBackend -eq "native"
				$cycles.Add([pscustomobject]@{
					cycle = $cycle
					open_response_media = Get-ObjectPropertyValue -Object $openResponse -Name "media"
					surface_activation_latency_ms = if ($null -ne $surfaceActivation) { $surfaceActivation.surface_activation_latency_ms } else { $null }
					renderer_ready_latency_ms = if ($null -ne $rendererReady) { $rendererReady.renderer_ready_latency_ms } else { $null }
					playback_ready_latency_ms = if ($null -ne $rendererReady) { $rendererReady.playback_ready_latency_ms } else { $null }
					activation_state = if ($null -ne $surfaceActivation) { $surfaceActivation.state } else { $null }
					activation_error = $activationError
					renderer_ready_state = if ($null -ne $rendererReady) { $rendererReady.state } else { $null }
					renderer_ready_error = $rendererReadyError
					renderer_backend = $rendererBackend
					native_backend_observed = $nativeBackendObserved
					renderer_process_observed = $rendererProcessObserved
					latched_qtwebengine_process_start_count = $cycleLatched.Count
					process_tree_after_activation = $treeAfterActivation
					close_latency_ms = if ($null -ne $close) { $close.close_latency_ms } else { $null }
					close_state = if ($null -ne $close) { $close.state } else { $null }
					close_error = $closeError
					process_tree_after_close = $treeAfterClose
					qtwebengine_process_persisted_after_close = $treeAfterClose.qtwebengine_process_count -gt 0
					passed = $null -ne $surfaceActivation -and $null -ne $rendererReady -and
						$nativeBackendObserved -and -not $rendererProcessObserved -and $null -ne $close
				})
				Start-Sleep -Milliseconds 100
			}

			if ($performanceSupported) {
				Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null
				$performanceResponse = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken `
					-Request @{ command = "qmlPerformanceSnapshot" }
				$performanceSnapshot = Get-ObjectPropertyValue -Object $performanceResponse -Name "performance"
			}
		} catch {
			$runError = $_.Exception.Message
			if ($performanceSupported) {
				try { Invoke-QmlAutomationCommand -Port $runPort -Token $runToken -Request @{ command = "qmlPerformanceEnd" } | Out-Null } catch { }
				try {
					$performanceResponse = Invoke-QmlAutomationCommand -Port $runPort -Token $runToken `
						-Request @{ command = "qmlPerformanceSnapshot" }
					$performanceSnapshot = Get-ObjectPropertyValue -Object $performanceResponse -Name "performance"
				} catch { }
			}
		} finally {
			if ($null -ne $trace) {
				foreach ($record in @(Receive-ProcessStartTrace -Trace $trace -DeliveryGraceMilliseconds 100)) {
					$allTraceRecords.Add($record)
				}
			}
			Stop-ProcessStartTrace -Trace $trace
			Stop-TestProcess -Process $process
		}

		$cycleValues = @($cycles.ToArray())
		$uiStallCount = if ($null -ne $performanceSnapshot) {
			[int](Get-ObjectPropertyValue -Object $performanceSnapshot -Name "uiStallCount" -Default -1)
		} else { $null }
		$runGates = [ordered]@{
			zero_qtwebengine_before_activation = $null -ne $preActivation -and $preActivation.zero_qtwebengine_before_activation
			all_cycles_surface_activated = $cycleValues.Count -eq $CyclesPerRun -and
				@($cycleValues | Where-Object { $null -eq $_.surface_activation_latency_ms }).Count -eq 0
			all_cycles_renderer_ready = $cycleValues.Count -eq $CyclesPerRun -and
				@($cycleValues | Where-Object { $null -eq $_.renderer_ready_latency_ms }).Count -eq 0
			all_cycles_playback_ready = $cycleValues.Count -eq $CyclesPerRun -and
				@($cycleValues | Where-Object { $null -eq $_.playback_ready_latency_ms }).Count -eq 0
			all_cycles_used_native_renderer = $cycleValues.Count -eq $CyclesPerRun -and
				@($cycleValues | Where-Object { -not $_.native_backend_observed }).Count -eq 0
			all_cycles_avoided_qtwebengine_process = $cycleValues.Count -eq $CyclesPerRun -and
				@($cycleValues | Where-Object { $_.renderer_process_observed }).Count -eq 0
			all_cycles_closed_session_and_surface = $cycleValues.Count -eq $CyclesPerRun -and
				@($cycleValues | Where-Object { $null -eq $_.close_state }).Count -eq 0
			zero_ui_stalls_when_supported = if ($performanceSupported) { $null -ne $uiStallCount -and $uiStallCount -eq 0 } else { $null }
		}
		$runPassed = [bool]$runGates.zero_qtwebengine_before_activation -and
			[bool]$runGates.all_cycles_surface_activated -and [bool]$runGates.all_cycles_renderer_ready -and
			[bool]$runGates.all_cycles_playback_ready -and
			[bool]$runGates.all_cycles_used_native_renderer -and
			[bool]$runGates.all_cycles_avoided_qtwebengine_process -and
			[bool]$runGates.all_cycles_closed_session_and_surface -and
			(-not $performanceSupported -or [bool]$runGates.zero_ui_stalls_when_supported) -and
			[string]::IsNullOrWhiteSpace($runError)
		$measurements.Add([pscustomobject]@{
			run = $run
			process_started = $null -ne $process
			process_id = if ($null -ne $process) { $process.Id } else { $null }
			automation_port = $runPort
			isolated_config = $true
			inline_card_fixture = $inlineCardFixture
			process_monitoring_mode = if ($null -ne $trace) { $trace.mode } else { $null }
			process_monitoring_fallback_reason = if ($null -ne $trace) { $trace.fallback_reason } else { $null }
			pre_activation = $preActivation
			cycles = $cycleValues
			performance = [ordered]@{
				supported = $performanceSupported
				not_measured_reason = $performanceReason
				snapshot = $performanceSnapshot
				ui_stall_count = $uiStallCount
			}
			latched_qtwebengine_process_start_count = @(Get-LatchedWebEngineProcessStarts `
				-RootProcessId $(if ($null -ne $process) { $process.Id } else { -1 }) `
				-ProcessStartRecords @($allTraceRecords.ToArray())).Count
			gates = $runGates
			passed = $runPassed
			error = $runError
		})
	}
} finally {
	$env:MUMBLE_MODERN_AUTOMATION_PORT = $savedEnvironment.MUMBLE_MODERN_AUTOMATION_PORT
	$env:MUMBLE_MODERN_AUTOMATION_TOKEN = $savedEnvironment.MUMBLE_MODERN_AUTOMATION_TOKEN
}

$runValues = @($measurements.ToArray())
$firstActivationLatencies = @($runValues | ForEach-Object {
	$firstCycle = @($_.cycles | Where-Object { $_.cycle -eq 1 } | Select-Object -First 1)
	if ($firstCycle.Count -eq 1 -and $null -ne $firstCycle[0].surface_activation_latency_ms) {
		[double]$firstCycle[0].surface_activation_latency_ms
	}
})
$allActivationLatencies = @($runValues | ForEach-Object { $_.cycles } | ForEach-Object {
	if ($null -ne $_.surface_activation_latency_ms) { [double]$_.surface_activation_latency_ms }
})
$medianFirstActivation = Get-Median -Values $firstActivationLatencies
$processIds = @($runValues | Where-Object { $null -ne $_.process_id } | ForEach-Object { [int]$_.process_id })
$performanceRuns = @($runValues | Where-Object { $_.performance.supported })
$gates = [ordered]@{
	exactly_five_fresh_process_runs = $freshProcessLaunchCount -eq 5 -and $runValues.Count -eq 5 -and
		$processIds.Count -eq 5 -and @($processIds | Select-Object -Unique).Count -eq 5
	zero_qtwebengine_before_first_activation = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.zero_qtwebengine_before_activation }).Count -eq 0
	all_inline_cycles_surface_activated = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.all_cycles_surface_activated }).Count -eq 0
	all_inline_cycles_renderer_ready = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.all_cycles_renderer_ready }).Count -eq 0
	all_inline_cycles_playback_ready = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.all_cycles_playback_ready }).Count -eq 0
	all_inline_cycles_used_native_renderer = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.all_cycles_used_native_renderer }).Count -eq 0
	all_inline_cycles_avoided_qtwebengine_process = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.all_cycles_avoided_qtwebengine_process }).Count -eq 0
	all_inline_cycles_closed_session_and_surface = $runValues.Count -eq 5 -and
		@($runValues | Where-Object { -not $_.gates.all_cycles_closed_session_and_surface }).Count -eq 0
	median_first_activation_at_most_50_ms = $firstActivationLatencies.Count -eq 5 -and
		$null -ne $medianFirstActivation -and $medianFirstActivation -le $firstActivationTargetMilliseconds
	zero_ui_stalls_when_performance_snapshot_supported = @($performanceRuns | Where-Object {
		-not [bool]$_.gates.zero_ui_stalls_when_supported
	}).Count -eq 0
}
$gates.passed = @($gates.GetEnumerator() | Where-Object { $_.Key -ne "passed" -and -not [bool]$_.Value }).Count -eq 0

$result = [ordered]@{
	schema_version = $schemaVersion
	contract_id = $contractId
	measured_at_utc = [DateTime]::UtcNow.ToString("o")
	executable = $executablePath
	source_config = $sourceConfigPath
	isolation = [ordered]@{
		auto_connect_disabled = $true
		update_checks_disabled = $true
		plugins_disabled = $true
		per_run_database = $true
		state_retained = [bool]$KeepIsolatedState
		state_root = if ($KeepIsolatedState) { $stateRoot } else { $null }
	}
	process_monitoring = [ordered]@{
		method = "continuous Win32_ProcessStartTrace with non-admin Toolhelp polling fallback, plus live descendant-tree snapshots"
		pre_activation_trace_armed_before_process_launch = $true
		zero_process_rule = "no descendant QtWebEngineProcess.exe start (including transient) and no live QtWebEngineProcess/--type=renderer"
	}
	media_fixture = [ordered]@{
		network_access = $false
		presentation = "inline"
		mime = "audio/wav"
		source = "bounded in-memory data URL"
		card_owner = "deterministic visible direct-media rich card"
		expected_renderer_backend = "native"
		qtwebengine_allowed = $false
	}
	thresholds = [ordered]@{
		runs = 5
		minimum_cycles_per_process = 2
		median_first_activation_ms = $firstActivationTargetMilliseconds
		ui_stall_count_when_supported = 0
	}
	measurements = $runValues
	summary = [ordered]@{
		fresh_process_launch_count = $freshProcessLaunchCount
		completed_run_count = $runValues.Count
		completed_cycle_count = @($runValues | ForEach-Object { $_.cycles }).Count
		first_activation_sample_count = $firstActivationLatencies.Count
		median_first_activation_latency_ms = $medianFirstActivation
		median_all_activation_latency_ms = Get-Median -Values $allActivationLatencies
		performance_snapshot_supported_run_count = $performanceRuns.Count
		post_close_qtwebengine_persistence_observed = @($runValues | ForEach-Object { $_.cycles } | Where-Object {
			$_.qtwebengine_process_persisted_after_close
		}).Count -gt 0
	}
	gates = $gates
}

$outputDirectory = Split-Path -Parent $outputFilePath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
[IO.File]::WriteAllText($outputFilePath, ($result | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))

if (-not $KeepIsolatedState) {
	Remove-Item -LiteralPath $stateRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not [bool]$gates.passed) {
	throw "Inline-media lifecycle gate failed. Diagnostics: $outputFilePath"
}

[pscustomobject]$result
