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
		sourceProvider = "youtube"
		sourceUrl = "https://www.youtube.com/watch?v=M7lc1UVf-VE"
		sourceItemId = "M7lc1UVf-VE"
		playbackItemId = "M7lc1UVf-VE"
		contentBranch = "video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "youtube"
		playbackUrl = "https://www.youtube-nocookie.com/embed/M7lc1UVf-VE?rel=0&modestbranding=1&playsinline=1&autoplay=1&controls=1&enablejsapi=1&origin=https%3A%2F%2Fwww.mumble.info&widget_referrer=https%3A%2F%2Fwww.mumble.info%2F"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "vimeo"
		sourceProvider = "vimeo"
		sourceUrl = "https://vimeo.com/1195621748"
		sourceItemId = "1195621748"
		playbackItemId = "1195621748"
		contentBranch = "video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "vimeo"
		playbackUrl = "https://player.vimeo.com/video/1195621748?title=0&byline=0&portrait=0&autoplay=1"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "twitch"
		sourceProvider = "twitch"
		sourceUrl = "https://clips.twitch.tv/IncredulousAbstemiousFennelImGlitch"
		sourceItemId = "IncredulousAbstemiousFennelImGlitch"
		playbackItemId = "IncredulousAbstemiousFennelImGlitch"
		contentBranch = "clip-video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "twitch"
		playbackUrl = "https://clips.twitch.tv/embed?clip=IncredulousAbstemiousFennelImGlitch&parent=www.mumble.info&autoplay=true&muted=true"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "streamable"
		sourceProvider = "streamable"
		sourceUrl = "https://streamable.com/ba9f2"
		sourceItemId = "ba9f2"
		playbackItemId = "ba9f2"
		contentBranch = "video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "streamable"
		playbackUrl = "https://streamable.com/e/ba9f2?autoplay=1"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "dailymotion"
		sourceProvider = "dailymotion"
		sourceUrl = "https://www.dailymotion.com/video/x84sh87"
		sourceItemId = "x84sh87"
		playbackItemId = "x84sh87"
		contentBranch = "video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "dailymotion"
		playbackUrl = "https://geo.dailymotion.com/player.html?video=x84sh87&autoplay=1&mute=1"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "soundcloud"
		sourceProvider = "soundcloud"
		sourceUrl = "https://soundcloud.com/forss/flickermood"
		sourceItemId = "forss/flickermood"
		playbackItemId = "forss/flickermood"
		contentBranch = "audio-track"
		expectedPresentation = "provider-audio-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "soundcloud"
		playbackUrl = "https://w.soundcloud.com/player/?url=https%3A%2F%2Fsoundcloud.com%2Fforss%2Fflickermood&auto_play=true&hide_related=false&show_comments=false&show_user=true&show_reposts=false&visual=false"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "spotify"
		sourceProvider = "spotify"
		sourceUrl = "https://open.spotify.com/album/2dFcS2u5YoUj4WmUkZ1oW6"
		sourceItemId = "album/2dFcS2u5YoUj4WmUkZ1oW6"
		playbackItemId = "album/2dFcS2u5YoUj4WmUkZ1oW6"
		contentBranch = "audio-album"
		expectedPresentation = "provider-audio-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "spotify"
		playbackUrl = "https://open.spotify.com/embed/album/2dFcS2u5YoUj4WmUkZ1oW6?utm_source=generator"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "tiktok"
		sourceProvider = "tiktok"
		sourceUrl = "https://www.tiktok.com/@imee_2001/video/7611978857305984274"
		sourceItemId = "7611978857305984274"
		playbackItemId = "7611978857305984274"
		contentBranch = "video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "tiktok"
		playbackUrl = "https://www.tiktok.com/player/v1/7611978857305984274?autoplay=1&rel=0&music_info=0&description=0"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "instagram"
		sourceProvider = "instagram"
		sourceUrl = "https://www.instagram.com/reel/DYWnW2RMbWr/"
		sourceItemId = "reel/DYWnW2RMbWr"
		playbackItemId = "reel/DYWnW2RMbWr"
		contentBranch = "reel-video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "instagram"
		playbackUrl = "https://www.instagram.com/reel/DYWnW2RMbWr/embed/"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "facebook"
		sourceProvider = "facebook"
		sourceUrl = "https://www.facebook.com/reel/1327313299204519"
		sourceItemId = "reel/1327313299204519"
		playbackItemId = "reel/1327313299204519"
		contentBranch = "reel-video"
		expectedPresentation = "provider-video-player"
		verificationSurface = "provider-webengine"
		coverageState = "real-playback"
		provider = "facebook"
		playbackUrl = "https://www.facebook.com/plugins/video.php?href=https%3A%2F%2Fwww.facebook.com%2Freel%2F1327313299204519&show_text=false&width=560"
		mediaMime = ""
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "reddit-direct"
		sourceProvider = "reddit"
		sourceUrl = "https://www.reddit.com/r/interestingasfuck/comments/1tobuah/a_snow_leopards_reaction_after_seeing_a_tiger/"
		sourceItemId = "1tobuah"
		playbackItemId = "2mhpjnavbi3h1"
		contentBranch = "video-with-separate-audio"
		expectedPresentation = "native-video-player"
		verificationSurface = "native-media"
		coverageState = "real-playback"
		provider = "direct"
		playbackUrl = "https://v.redd.it/2mhpjnavbi3h1/CMAF_360.mp4"
		mediaMime = "video/mp4"
		audioUrl = "https://v.redd.it/2mhpjnavbi3h1/CMAF_AUDIO_128.mp4"
		audioMime = "audio/mp4"
	}
	[pscustomobject]@{
		id = "x-direct"
		sourceProvider = "x"
		sourceUrl = "https://x.com/historyinmemes/status/2058971862265151767"
		sourceItemId = "2058971862265151767"
		playbackItemId = "2056919877567291392"
		contentBranch = "video"
		expectedPresentation = "native-video-player"
		verificationSurface = "native-media"
		coverageState = "real-playback"
		provider = "direct"
		playbackUrl = "https://video.twimg.com/amplify_video/2056919877567291392/vid/avc1/680x784/24c6nHGlHaoG23fa.mp4"
		mediaMime = "video/mp4"
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "x-animated-gif"
		sourceProvider = "x"
		sourceUrl = "https://x.com/FloodSocial/status/870042717589340160"
		sourceItemId = "870042717589340160"
		playbackItemId = "DBMDLy_U0AAqUWP"
		contentBranch = "animated-gif-video-backed"
		expectedPresentation = "animated-image"
		verificationSurface = "native-media-transport"
		coverageState = "real-transport-playback"
		provider = "direct"
		playbackUrl = "https://video.twimg.com/tweet_video/DBMDLy_U0AAqUWP.mp4"
		mediaMime = "video/mp4"
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "reddit-gif"
		sourceProvider = "reddit"
		sourceUrl = "https://www.reddit.com/r/animegifs/comments/g91mkj/slap_me_with_the_money_kon/"
		sourceItemId = "g91mkj"
		playbackItemId = "p91cxpzry9v41"
		contentBranch = "animated-gif-video-backed"
		expectedPresentation = "animated-image"
		verificationSurface = "native-media-transport"
		coverageState = "real-transport-playback"
		provider = "direct"
		playbackUrl = "https://v.redd.it/p91cxpzry9v41/DASH_1080?source=fallback"
		mediaMime = "video/mp4"
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "tenor-gif"
		sourceProvider = "tenor"
		sourceUrl = "https://tenor.com/view/what-is-this-gif-2935825949418015718"
		sourceItemId = "2935825949418015718"
		playbackItemId = "KL4mJXVvS-YAAAPo"
		contentBranch = "animated-gif-video-backed"
		expectedPresentation = "animated-image"
		verificationSurface = "native-media-transport"
		coverageState = "real-transport-playback"
		provider = "direct"
		playbackUrl = "https://media.tenor.com/KL4mJXVvS-YAAAPo/what-is-this.mp4"
		mediaMime = "video/mp4"
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "imgur-gifv"
		sourceProvider = "imgur"
		sourceUrl = "https://imgur.com/owLfF25"
		sourceItemId = "owLfF25"
		playbackItemId = "owLfF25"
		contentBranch = "animated-gif-video-backed"
		expectedPresentation = "animated-image"
		verificationSurface = "native-media-transport"
		coverageState = "real-transport-playback"
		provider = "direct"
		playbackUrl = "https://i.imgur.com/owLfF25.mp4"
		mediaMime = "video/mp4"
		audioUrl = ""
		audioMime = ""
	}
	[pscustomobject]@{
		id = "4chan-direct"
		sourceProvider = "4chan"
		sourceUrl = "https://i.4cdn.org/wsg/1779181872711838.webm"
		sourceItemId = "1779181872711838"
		playbackItemId = "1779181872711838"
		contentBranch = "video"
		expectedPresentation = "native-video-player"
		verificationSurface = "native-media"
		coverageState = "real-playback"
		provider = "direct"
		playbackUrl = "https://i.4cdn.org/wsg/1779181872711838.webm"
		mediaMime = "video/webm"
		audioUrl = ""
		audioMime = ""
	}
)

$formatCoverage = @(
	foreach ($mediaCase in $cases) {
		[pscustomobject]@{
			id = $mediaCase.id
			source_provider = $mediaCase.sourceProvider
			source_url = $mediaCase.sourceUrl
			playback_url = $mediaCase.playbackUrl
			source_item_id = $mediaCase.sourceItemId
			playback_item_id = $mediaCase.playbackItemId
			content_branch = $mediaCase.contentBranch
			expected_presentation = $mediaCase.expectedPresentation
			verification_surface = $mediaCase.verificationSurface
			coverage_state = $mediaCase.coverageState
		}
	}
	[pscustomobject]@{
		id = "instagram-static"
		source_provider = "instagram"
		source_url = "https://www.instagram.com/p/DYzqx_9txNX/"
		playback_url = "https://www.instagram.com/p/DYzqx_9txNX/embed/"
		source_item_id = "p/DYzqx_9txNX"
		playback_item_id = "p/DYzqx_9txNX"
		content_branch = "still-post"
		expected_presentation = "provider-post-viewer"
		verification_surface = "persistent-chat-card"
		coverage_state = "real-cache-observation"
	}
	[pscustomobject]@{
		id = "reddit-still"
		source_provider = "reddit"
		source_url = "https://www.reddit.com/r/pics/comments/haucpf/ive_found_a_few_funny_memories_during_lockdown/"
		playback_url = "https://i.redd.it/f58v4g8mwh551.jpg"
		source_item_id = "haucpf"
		playback_item_id = "f58v4g8mwh551"
		content_branch = "still-image"
		expected_presentation = "image-card"
		verification_surface = "persistent-chat-card"
		coverage_state = "runtime-pending"
	}
	[pscustomobject]@{
		id = "x-still"
		source_provider = "x"
		source_url = "https://x.com/tekbog/status/2058911571225813258"
		playback_url = "https://pbs.twimg.com/media/HJK48cubUAEIN2b.png"
		source_item_id = "2058911571225813258"
		playback_item_id = "HJK48cubUAEIN2b"
		content_branch = "still-image"
		expected_presentation = "image-card"
		verification_surface = "persistent-chat-card"
		coverage_state = "real-cache-observation"
	}
	[pscustomobject]@{
		id = "giphy-gif"
		source_provider = "giphy"
		source_url = "https://giphy.com/gifs/bbqfilms-ghostbusters-ecto-cooler-see-the-slime-xT4uQCfBOBGralHfOM"
		playback_url = "https://media.giphy.com/media/xT4uQCfBOBGralHfOM/giphy.gif"
		source_item_id = "xT4uQCfBOBGralHfOM"
		playback_item_id = "xT4uQCfBOBGralHfOM"
		content_branch = "animated-gif"
		expected_presentation = "animated-image"
		verification_surface = "persistent-chat-card"
		coverage_state = "runtime-pending"
	}
	[pscustomobject]@{
		id = "tiktok-photo"
		source_provider = "tiktok"
		source_url = "https://www.tiktok.com/@contextify0/photo/7626953410033585428"
		playback_url = ""
		source_item_id = "7626953410033585428"
		playback_item_id = ""
		content_branch = "photo-carousel"
		expected_presentation = "provider-post-card"
		verification_surface = "persistent-chat-card"
		coverage_state = "runtime-pending"
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
			url = $MediaCase.playbackUrl
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
		$playbackCaptureAttempted = $false
		$playbackCapturePath = ""
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
				if ($playbackObserved -and -not $playbackCaptureAttempted) {
					$playbackCaptureAttempted = $true
					$playingCaptureCandidate =
						Join-Path $outputRoot "$($MediaCase.id)-$Pass-playing.png"
					try {
						Invoke-Automation -Port $port -Token $token -Request @{
							command = "captureQml"
							window = "media-session"
							path = $playingCaptureCandidate
						} | Out-Null
						$playbackCapturePath = $playingCaptureCandidate
					} catch {
						$playbackCapturePath = ""
					}
				}
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
			source_provider = $MediaCase.sourceProvider
			source_url = $MediaCase.sourceUrl
			playback_url = $MediaCase.playbackUrl
			source_item_id = $MediaCase.sourceItemId
			playback_item_id = $MediaCase.playbackItemId
			content_branch = $MediaCase.contentBranch
			expected_presentation = $MediaCase.expectedPresentation
			verification_surface = $MediaCase.verificationSurface
			coverage_state = $MediaCase.coverageState
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
			playback_capture = $playbackCapturePath
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
	format_coverage = @($formatCoverage)
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
