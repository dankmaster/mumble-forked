[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Executable,

	[Parameter(Mandatory = $true)]
	[string]$ConfigPath,

	[string[]]$Case = @(),
	[int]$StartupTimeoutSeconds = 60,
	[int]$MediaTimeoutSeconds = 30,
	[int]$PlaybackObservationSeconds = 8,
	[int]$PostCloseObservationSeconds = 6,
	[int]$SampleIntervalMilliseconds = 250,
	[int64]$MemoryAbortBytes = 2GB,
	[string]$OutputPath = ".tmp\qml-real-media-playback",
	[switch]$KeepIsolatedState
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$sourceConfigPath = (Resolve-Path -LiteralPath $ConfigPath).Path
$stageRoot = Split-Path -Parent $executablePath
$outputBase = [IO.Path]::GetFullPath($OutputPath)
$runSuffix = (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$outputRoot = Join-Path $outputBase $runSuffix
$stateRoot = Join-Path $outputRoot "isolated-state"
$providerStateRoot = Join-Path $stateRoot "media-provider-state"
$configPath = Join-Path $stateRoot "mumble_settings.json"

if ($StartupTimeoutSeconds -lt 1) { throw "StartupTimeoutSeconds must be positive." }
if ($MediaTimeoutSeconds -lt 1) { throw "MediaTimeoutSeconds must be positive." }
if ($PlaybackObservationSeconds -lt 2) {
	throw "PlaybackObservationSeconds must be at least two seconds."
}
if ($PostCloseObservationSeconds -lt 2 -or $PostCloseObservationSeconds -gt 30) {
	throw "PostCloseObservationSeconds must be between two and thirty seconds."
}
if ($SampleIntervalMilliseconds -lt 50 -or $SampleIntervalMilliseconds -gt 2000) {
	throw "SampleIntervalMilliseconds must be between 50 and 2000 ms."
}
if ($MemoryAbortBytes -lt 256MB) { throw "MemoryAbortBytes must be at least 256 MiB." }

$cases = @(
	[pscustomobject]@{
		id = "youtube"
		provider = "youtube"
		url = "https://www.youtube-nocookie.com/embed/M7lc1UVf-VE?rel=0&modestbranding=1&playsinline=1&autoplay=1&controls=1&enablejsapi=1&origin=https%3A%2F%2Fwww.mumble.info&widget_referrer=https%3A%2F%2Fwww.mumble.info%2F"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "vimeo"
		provider = "vimeo"
		url = "https://player.vimeo.com/video/1195621748?title=0&byline=0&portrait=0&autoplay=1"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "twitch"
		provider = "twitch"
		url = "https://clips.twitch.tv/embed?clip=IncredulousAbstemiousFennelImGlitch&parent=www.mumble.info&autoplay=true&muted=true"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "streamable"
		provider = "streamable"
		url = "https://streamable.com/e/ba9f2?autoplay=1"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "dailymotion"
		provider = "dailymotion"
		url = "https://geo.dailymotion.com/player.html?video=x84sh87&autoplay=1&mute=1"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "soundcloud"
		provider = "soundcloud"
		url = "https://w.soundcloud.com/player/?url=https%3A%2F%2Fsoundcloud.com%2Fforss%2Fflickermood&auto_play=true&hide_related=false&show_comments=false&show_user=true&show_reposts=false&visual=false"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "spotify"
		provider = "spotify"
		url = "https://open.spotify.com/embed/album/2dFcS2u5YoUj4WmUkZ1oW6?utm_source=generator"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "tiktok"
		provider = "tiktok"
		url = "https://www.tiktok.com/player/v1/7611978857305984274?autoplay=1&rel=0&music_info=0&description=0"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "instagram"
		provider = "instagram"
		url = "https://www.instagram.com/reel/DYWnW2RMbWr/embed/"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "facebook"
		provider = "facebook"
		url = "https://www.facebook.com/plugins/video.php?href=https%3A%2F%2Fwww.facebook.com%2Freel%2F1327313299204519&show_text=false&width=560"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "reddit-direct"
		provider = "direct"
		url = "https://v.redd.it/2mhpjnavbi3h1/CMAF_360.mp4"
		mediaMime = "video/mp4"
		audioUrl = "https://v.redd.it/2mhpjnavbi3h1/CMAF_AUDIO_128.mp4"
		audioMime = "audio/mp4"
	}
	[pscustomobject]@{
		id = "x-direct"
		provider = "direct"
		url = "https://video.twimg.com/amplify_video/2056919877567291392/vid/avc1/680x784/24c6nHGlHaoG23fa.mp4"
		mediaMime = "video/mp4"
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "4chan-direct"
		provider = "direct"
		url = "https://i.4cdn.org/wsg/1779181872711838.webm"
		mediaMime = "video/webm"
		audioUrl = ""
		audioMime = ""
	}
)

if ($Case.Count -gt 0) {
	$unknownCases = @($Case | Where-Object { $_ -notin $cases.id })
	if ($unknownCases.Count -gt 0) {
		throw "Unknown media case(s): $($unknownCases -join ', '). Available: $($cases.id -join ', ')."
	}
	$cases = @($cases | Where-Object { $_.id -in $Case })
}

function Get-FreeTcpPort {
	$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
}

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
	$value = Get-ObjectPropertyValue -Object $Object -Name $Name
	if ($null -ne $value) { return $value }
	$value = [pscustomobject]@{}
	Set-ObjectPropertyValue -Object $Object -Name $Name -Value $value
	return $value
}

function New-IsolatedConfig {
	param(
		[Parameter(Mandatory = $true)][string]$SourcePath,
		[Parameter(Mandatory = $true)][string]$StateDirectory,
		[Parameter(Mandatory = $true)][string]$DestinationPath
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
	Set-ObjectPropertyValue -Object $misc -Name "database_location" `
		-Value ((Join-Path $StateDirectory "mumble.sqlite") -replace "\\", "/")
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

	[IO.File]::WriteAllText(
		$DestinationPath,
		($config | ConvertTo-Json -Depth 100),
		[Text.UTF8Encoding]::new($false)
	)
	return $DestinationPath
}

function Invoke-Automation {
	param(
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][string]$Token,
		[Parameter(Mandatory = $true)][hashtable]$Request,
		[int]$TimeoutMilliseconds = 12000
	)
	$payload = @{}
	foreach ($key in $Request.Keys) { $payload[$key] = $Request[$key] }
	$payload.token = $Token
	$json = $payload | ConvertTo-Json -Depth 50 -Compress
	$client = [Net.Sockets.TcpClient]::new()
	$pending = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
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
			$writer.WriteLine($json)
			$writer.Flush()
			$line = $reader.ReadLine()
			if ([string]::IsNullOrWhiteSpace($line)) { throw "Automation returned an empty response." }
			$response = $line | ConvertFrom-Json
			if (-not [bool]$response.ok) {
				throw "Automation command '$($Request.command)' failed: $($response.error)"
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

function Wait-Automation {
	param(
		[int]$Port,
		[string]$Token,
		[Diagnostics.Process]$Process,
		[int]$TimeoutSeconds
	)
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$lastError = ""
	do {
		$Process.Refresh()
		if ($Process.HasExited) { throw "Mumble exited during startup with code $($Process.ExitCode)." }
		try {
			return Invoke-Automation -Port $Port -Token $Token `
				-Request @{ command = "ping" } -TimeoutMilliseconds 1000
		} catch {
			$lastError = $_.Exception.Message
			Start-Sleep -Milliseconds 100
		}
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Automation did not become ready: $lastError"
}

function Get-DescendantProcessIds {
	param([int]$RootProcessId)
	$processes = @(Get-CimInstance Win32_Process -ErrorAction Stop)
	$children = @{}
	foreach ($entry in $processes) {
		$parent = [int]$entry.ParentProcessId
		if (-not $children.ContainsKey($parent)) {
			$children[$parent] = [Collections.Generic.List[int]]::new()
		}
		$children[$parent].Add([int]$entry.ProcessId)
	}
	$result = [Collections.Generic.List[int]]::new()
	$queue = [Collections.Generic.Queue[int]]::new()
	$queue.Enqueue($RootProcessId)
	while ($queue.Count -gt 0) {
		$current = $queue.Dequeue()
		if ($result.Contains($current)) { continue }
		$result.Add($current)
		if ($children.ContainsKey($current)) {
			foreach ($child in $children[$current]) { $queue.Enqueue($child) }
		}
	}
	return @($result)
}

$script:previousCpu = @{}
$script:previousSampleTime = $null
function Get-ResourceSample {
	param([int]$RootProcessId, [string]$Phase, [string]$CaseId, [string]$Pass)
	$now = [DateTime]::UtcNow
	$ids = @(Get-DescendantProcessIds -RootProcessId $RootProcessId)
	$workingSet = [int64]0
	$privateBytes = [int64]0
	$cpuDeltaSeconds = 0.0
	$names = [Collections.Generic.List[string]]::new()
	$currentCpu = @{}
	foreach ($id in $ids) {
		$process = Get-Process -Id $id -ErrorAction SilentlyContinue
		if (-not $process) { continue }
		$process.Refresh()
		$workingSet += [int64]$process.WorkingSet64
		$privateBytes += [int64]$process.PrivateMemorySize64
		$cpu = if ($null -eq $process.CPU) { 0.0 } else { [double]$process.CPU }
		$currentCpu[$id] = $cpu
		if ($script:previousCpu.ContainsKey($id)) {
			$cpuDeltaSeconds += [Math]::Max(0.0, $cpu - [double]$script:previousCpu[$id])
		}
		$names.Add($process.ProcessName)
	}
	$elapsedSeconds = if ($null -eq $script:previousSampleTime) {
		0.0
	} else {
		($now - $script:previousSampleTime).TotalSeconds
	}
	$aggregateCpuPercent = if ($elapsedSeconds -gt 0.0) {
		100.0 * $cpuDeltaSeconds / $elapsedSeconds
	} else {
		0.0
	}
	$script:previousCpu = $currentCpu
	$script:previousSampleTime = $now
	if ($workingSet -gt $memoryAbortBytes) {
		throw "Process tree exceeded memory guard: $workingSet bytes."
	}
	return [pscustomobject]@{
		timestamp_utc = $now.ToString("o")
		case_id = $CaseId
		pass = $Pass
		phase = $Phase
		process_count = $ids.Count
		process_names = @($names | Sort-Object -Unique)
		working_set_bytes = $workingSet
		private_bytes = $privateBytes
		aggregate_cpu_percent = $aggregateCpuPercent
	}
}

function Get-SampleSummary {
	param([object[]]$Samples)
	if ($Samples.Count -eq 0) { return $null }
	$working = @($Samples | ForEach-Object { [double]$_.working_set_bytes } | Sort-Object)
	$private = @($Samples | ForEach-Object { [double]$_.private_bytes } | Sort-Object)
	$cpu = @($Samples | ForEach-Object { [double]$_.aggregate_cpu_percent } | Sort-Object)
	$p95Index = [Math]::Min($Samples.Count - 1, [Math]::Ceiling($Samples.Count * 0.95) - 1)
	return [pscustomobject]@{
		sample_count = $Samples.Count
		peak_working_set_bytes = [int64]($working | Measure-Object -Maximum).Maximum
		p95_working_set_bytes = [int64]$working[$p95Index]
		peak_private_bytes = [int64]($private | Measure-Object -Maximum).Maximum
		p95_aggregate_cpu_percent = [double]$cpu[$p95Index]
		peak_aggregate_cpu_percent = [double]($cpu | Measure-Object -Maximum).Maximum
	}
}

function Wait-MediaClosed {
	param([int]$Port, [string]$Token)
	$deadline = [DateTime]::UtcNow.AddSeconds(12)
	do {
		$state = Invoke-Automation -Port $Port -Token $Token `
			-Request @{ command = "qmlReadinessState" }
		if (-not [bool]$state.mediaActive -and -not [bool]$state.media.rendererPresent) { return $state }
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Media surface remained after close."
}

function Stop-Client {
	param([Diagnostics.Process]$Process)
	if (-not $Process) { return }
	$Process.Refresh()
	if ($Process.HasExited) { return }
	[void]$Process.CloseMainWindow()
	if (-not $Process.WaitForExit(5000)) {
		Stop-Process -Id $Process.Id -Force
		[void]$Process.WaitForExit(5000)
	}
}

function Get-ProviderStateSnapshot {
	param([string]$Provider)
	$path = Join-Path $providerStateRoot $Provider
	$files = @(if (Test-Path -LiteralPath $path -PathType Container) {
		Get-ChildItem -LiteralPath $path -File -Recurse -ErrorAction SilentlyContinue
	})
	$totalBytes = if ($files.Count -gt 0) {
		[long](($files | Measure-Object -Property Length -Sum).Sum)
	} else {
		[long]0
	}
	return [pscustomobject]@{
		path = $path
		exists = Test-Path -LiteralPath $path -PathType Container
		file_count = $files.Count
		bytes = [int64]$totalBytes
	}
}

function Get-PlaybackOutcome {
	param(
		[object]$Media,
		[bool]$PlaybackObserved
	)
	$verification = [string]$Media.surfaceVerificationState
	if ($verification -eq "blocked") { return "verification-required" }
	if ($verification -eq "failed" -or -not [string]::IsNullOrWhiteSpace([string]$Media.error)) {
		return "failed"
	}
	if ($verification -ne "verified") { return "timed-out" }
	if ($PlaybackObserved) { return "playing" }
	if ([bool]$Media.transportVerified) { return "ready-not-playing" }
	return "unverified"
}

function Invoke-RealMediaPass {
	param(
		[Parameter(Mandatory = $true)][object]$MediaCase,
		[Parameter(Mandatory = $true)][ValidateSet("cold", "warm")][string]$Pass
	)

	$port = Get-FreeTcpPort
	$token = [guid]::NewGuid().ToString("N")
	$savedEnvironment = @{}
	$environment = @{
		MUMBLE_MODERN_AUTOMATION_PORT = [string]$port
		MUMBLE_MODERN_AUTOMATION_TOKEN = $token
		MUMBLE_MEDIA_PROFILE_ROOT = $providerStateRoot
		QTWEBENGINEPROCESS_PATH = (Join-Path $stageRoot "QtWebEngineProcess.exe")
		QTWEBENGINE_RESOURCES_PATH = (Join-Path $stageRoot "resources")
		QTWEBENGINE_LOCALES_PATH = (Join-Path $stageRoot "translations\qtwebengine_locales")
		PATH = ((Join-Path $stageRoot "gstreamer\bin") + ";" + $stageRoot + ";" + $env:PATH)
		GST_PLUGIN_SYSTEM_PATH_1_0 = (Join-Path $stageRoot "gstreamer\lib\gstreamer-1.0")
		GST_PLUGIN_SCANNER = (Join-Path $stageRoot "gstreamer\libexec\gstreamer-1.0\gst-plugin-scanner.exe")
	}
	foreach ($name in $environment.Keys) {
		$savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
		[Environment]::SetEnvironmentVariable($name, $environment[$name], "Process")
	}

	$stateBefore = Get-ProviderStateSnapshot -Provider $MediaCase.provider
	$process = $null
	try {
		$startupClock = [Diagnostics.Stopwatch]::StartNew()
		$process = Start-Process -FilePath $executablePath `
			-ArgumentList @("--multiple", "--config", $configPath, "--skip-settings-backup-prompt") `
			-WorkingDirectory $stageRoot -PassThru
		Wait-Automation -Port $port -Token $token -Process $process `
			-TimeoutSeconds $StartupTimeoutSeconds | Out-Null
		$startupMilliseconds = $startupClock.Elapsed.TotalMilliseconds
		Invoke-Automation -Port $port -Token $token -Request @{
			command = "setHostViewport"
			width = 1280
			height = 900
			async = $false
		} | Out-Null

		$script:previousCpu = @{}
		$script:previousSampleTime = $null
		$samples = [Collections.Generic.List[object]]::new()
		$openClock = [Diagnostics.Stopwatch]::StartNew()
		Invoke-Automation -Port $port -Token $token -Request @{
			command = "openQmlMediaSession"
			presentation = "detached"
			url = $MediaCase.url
			provider = $MediaCase.provider
			mediaMime = $MediaCase.mediaMime
			audioUrl = $MediaCase.audioUrl
			audioMime = $MediaCase.audioMime
			sessionId = "real-media:$($MediaCase.id):$Pass"
		} | Out-Null

		$loadDeadline = [DateTime]::UtcNow.AddSeconds($MediaTimeoutSeconds)
		$observationDeadline = $null
		$terminalMilliseconds = $null
		$playRequested = $false
		$playRequestHandled = $false
		$initialPosition = $null
		$maximumPosition = 0.0
		$playbackObserved = $false
		$finalState = $null
		while ($true) {
			$phase = if ($null -eq $observationDeadline) { "media-load" } else { "playback" }
			$samples.Add((Get-ResourceSample -RootProcessId $process.Id -Phase $phase `
				-CaseId $MediaCase.id -Pass $Pass))
			$state = Invoke-Automation -Port $port -Token $token `
				-Request @{ command = "qmlReadinessState" }
			$media = $state.media
			$verification = [string]$media.surfaceVerificationState
			$maximumPosition = [Math]::Max($maximumPosition, [double]$media.position)

			if ($verification -in @("blocked", "failed")) {
				if ($null -eq $terminalMilliseconds) {
					$terminalMilliseconds = $openClock.Elapsed.TotalMilliseconds
				}
				$finalState = $media
				break
			}

			if ($verification -eq "verified") {
				if ($null -eq $observationDeadline) {
					$terminalMilliseconds = $openClock.Elapsed.TotalMilliseconds
					$observationDeadline =
						[DateTime]::UtcNow.AddSeconds($PlaybackObservationSeconds)
					$initialPosition = [double]$media.position
				}
				if (-not $playRequested -and [bool]$media.playbackControlAllowed) {
					$playRequested = $true
					$playResponse = Invoke-Automation -Port $port -Token $token -Request @{
						command = "watchTogetherAction"
						action = "play"
					}
					$playRequestHandled = [bool]$playResponse.handled
				}
				$positionAdvanced = $null -ne $initialPosition `
					-and $maximumPosition - $initialPosition -ge 0.5
				$playbackObserved = $playbackObserved -or [bool]$media.playbackVerified `
					-or $positionAdvanced
				if ([DateTime]::UtcNow -ge $observationDeadline) {
					$finalState = $media
					break
				}
			} elseif ([DateTime]::UtcNow -ge $loadDeadline) {
				$finalState = $media
				break
			}
			Start-Sleep -Milliseconds $SampleIntervalMilliseconds
		}
		if ($null -eq $finalState) {
			$finalState = (Invoke-Automation -Port $port -Token $token `
				-Request @{ command = "qmlReadinessState" }).media
		}
		if ($null -eq $terminalMilliseconds) {
			$terminalMilliseconds = $openClock.Elapsed.TotalMilliseconds
		}

		$capturePath = Join-Path $outputRoot "$($MediaCase.id)-$Pass.png"
		try {
			Invoke-Automation -Port $port -Token $token -Request @{
				command = "captureQml"
				window = "media-session"
				path = $capturePath
			} | Out-Null
		} catch {
			$capturePath = ""
		}

		$beforeClose = Invoke-Automation -Port $port -Token $token `
			-Request @{ command = "qmlReadinessState" }
		$maximumPosition = [Math]::Max($maximumPosition, [double]$beforeClose.media.position)
		$positionAdvanced = $null -ne $initialPosition `
			-and $maximumPosition - $initialPosition -ge 0.5
		$playbackObserved = $playbackObserved -or [bool]$beforeClose.media.playbackVerified `
			-or $positionAdvanced

		$closeClock = [Diagnostics.Stopwatch]::StartNew()
		Invoke-Automation -Port $port -Token $token `
			-Request @{ command = "closeQmlMediaSession" } | Out-Null
		Wait-MediaClosed -Port $port -Token $token | Out-Null
		$closeMilliseconds = $closeClock.Elapsed.TotalMilliseconds
		$postCloseDeadline = [DateTime]::UtcNow.AddSeconds($PostCloseObservationSeconds)
		while ([DateTime]::UtcNow -lt $postCloseDeadline) {
			$samples.Add((Get-ResourceSample -RootProcessId $process.Id -Phase "post-close" `
				-CaseId $MediaCase.id -Pass $Pass))
			Start-Sleep -Milliseconds $SampleIntervalMilliseconds
		}
		$postClose = Invoke-Automation -Port $port -Token $token `
			-Request @{ command = "qmlReadinessState" }
		$postCloseSample = @($samples | Where-Object phase -eq "post-close")[-1]
		$processExitClock = [Diagnostics.Stopwatch]::StartNew()
		Stop-Client -Process $process
		$processExited = $process.HasExited
		$process = $null
		$stateAfter = Get-ProviderStateSnapshot -Provider $MediaCase.provider

		$result = [pscustomobject]@{
			id = $MediaCase.id
			pass = $Pass
			provider = $MediaCase.provider
			url = $MediaCase.url
			audio_url = $MediaCase.audioUrl
			startup_ms = $startupMilliseconds
			terminal_ms = $terminalMilliseconds
			terminal_state = [string]$finalState.surfaceVerificationState
			terminal_evidence = [string]$finalState.surfaceVerificationEvidence
			terminal_detail = [string]$finalState.surfaceVerificationDetail
			controller_state = [string]$beforeClose.media.state
			error_code = [string]$finalState.errorCode
			error = [string]$finalState.error
			renderer_backend = [string]$finalState.rendererBackend
			transport_verified = [bool]$finalState.transportVerified
			playback_verified = [bool]$finalState.playbackVerified
			play_request_sent = $playRequested
			play_request_handled = $playRequestHandled
			playback_observed = $playbackObserved
			position_start = if ($null -eq $initialPosition) { 0.0 } else { $initialPosition }
			position_max = $maximumPosition
			position_advanced = $positionAdvanced
			duration_before_close = [double]$beforeClose.media.duration
			outcome = Get-PlaybackOutcome -Media $finalState -PlaybackObserved $playbackObserved
			close_ms = $closeMilliseconds
			process_exit_ms = $processExitClock.Elapsed.TotalMilliseconds
			process_exited = $processExited
			surface_released = -not [bool]$postClose.media.rendererPresent
			media_inactive = -not [bool]$postClose.mediaActive
			post_close_process_count = $postCloseSample.process_count
			post_close_process_names = $postCloseSample.process_names
			post_close_working_set_bytes = $postCloseSample.working_set_bytes
			provider_state_before = $stateBefore
			provider_state_after = $stateAfter
			capture = $capturePath
			resources = Get-SampleSummary -Samples @($samples)
			samples = @($samples)
		}
		$result | ConvertTo-Json -Depth 30 |
			Set-Content -LiteralPath (Join-Path $outputRoot "$($MediaCase.id)-$Pass.json") -Encoding utf8
		return $result
	} finally {
		if ($process) { Stop-Client -Process $process }
		foreach ($name in $savedEnvironment.Keys) {
			[Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], "Process")
		}
	}
}

New-Item -ItemType Directory -Force -Path $outputRoot, $stateRoot, $providerStateRoot | Out-Null
New-IsolatedConfig -SourcePath $sourceConfigPath -StateDirectory $stateRoot `
	-DestinationPath $configPath | Out-Null

$allResults = [Collections.Generic.List[object]]::new()
$comparisons = [Collections.Generic.List[object]]::new()
foreach ($mediaCase in $cases) {
	Write-Host "REAL_MEDIA_START $($mediaCase.id)"
	$cold = Invoke-RealMediaPass -MediaCase $mediaCase -Pass "cold"
	$allResults.Add($cold)
	Write-Host ("REAL_MEDIA_PASS {0}/cold outcome={1} playback={2} peakMB={3:N1}" -f
		$mediaCase.id, $cold.outcome, $cold.playback_observed,
		($cold.resources.peak_working_set_bytes / 1MB))
	$warm = Invoke-RealMediaPass -MediaCase $mediaCase -Pass "warm"
	$allResults.Add($warm)
	Write-Host ("REAL_MEDIA_PASS {0}/warm outcome={1} playback={2} peakMB={3:N1}" -f
		$mediaCase.id, $warm.outcome, $warm.playback_observed,
		($warm.resources.peak_working_set_bytes / 1MB))
	$comparisons.Add([pscustomobject]@{
		id = $mediaCase.id
		cold_outcome = $cold.outcome
		warm_outcome = $warm.outcome
		cold_terminal_ms = $cold.terminal_ms
		warm_terminal_ms = $warm.terminal_ms
		terminal_delta_ms = $warm.terminal_ms - $cold.terminal_ms
		cold_peak_working_set_bytes = $cold.resources.peak_working_set_bytes
		warm_peak_working_set_bytes = $warm.resources.peak_working_set_bytes
		provider_state_reused = $warm.provider_state_before.exists `
			-and $warm.provider_state_before.file_count -gt 0
	})
}

$contractPassed = @($allResults | Where-Object {
	-not $_.surface_released -or -not $_.media_inactive -or -not $_.process_exited
}).Count -eq 0
$report = [pscustomobject]@{
	schema_version = 2
	contract = "windows-qml-real-media-playback-v2"
	generated_utc = [DateTime]::UtcNow.ToString("o")
	executable = $executablePath
	executable_sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $executablePath).Hash
	source_config = $sourceConfigPath
	output_root = $outputRoot
	state_root = $stateRoot
	provider_state_root = $providerStateRoot
	state_retained = [bool]$KeepIsolatedState
	sample_interval_ms = $SampleIntervalMilliseconds
	post_close_observation_seconds = $PostCloseObservationSeconds
	memory_abort_bytes = $MemoryAbortBytes
	network_results_are_environment_dependent = $true
	contract_passed = $contractPassed
	comparisons = @($comparisons)
	cases = @($allResults)
}
$reportPath = Join-Path $outputRoot "report.json"
$report | ConvertTo-Json -Depth 40 |
	Set-Content -LiteralPath $reportPath -Encoding utf8

if (-not $KeepIsolatedState) {
	$resolvedOutput = [IO.Path]::GetFullPath($outputRoot).TrimEnd('\') + '\'
	$resolvedState = [IO.Path]::GetFullPath($stateRoot)
	if (-not $resolvedState.StartsWith($resolvedOutput, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Refusing to remove isolated state outside the verifier output root: $resolvedState"
	}
	Remove-Item -LiteralPath $resolvedState -Recurse -Force
}

Write-Host "REAL_MEDIA_REPORT $reportPath"
$allResults | Select-Object id, pass, outcome, playback_observed,
	@{ n = "peak_mb"; e = { [Math]::Round($_.resources.peak_working_set_bytes / 1MB, 1) } },
	@{ n = "cpu_p95"; e = { [Math]::Round($_.resources.p95_aggregate_cpu_percent, 1) } },
	terminal_ms, close_ms | Format-Table -AutoSize
if (-not $contractPassed) {
	throw "The real-media lifecycle contract failed. See $reportPath"
}
