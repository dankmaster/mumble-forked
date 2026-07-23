$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$verifyScript = Join-Path $repoRoot 'scripts\windows\verify-qml-real-media-playback.ps1'
$profileFactory = Join-Path $repoRoot 'src\mumble\QmlMediaProfileFactory.cpp'
$playbackProbe = Join-Path $repoRoot 'src\mumble\qml-shell\MediaPlaybackProbe.js'

Describe 'Qt Quick real-media playback verifier' {
	BeforeAll {
		$scriptText = Get-Content -Raw -LiteralPath $verifyScript
		$profileFactoryText = Get-Content -Raw -LiteralPath $profileFactory
		$playbackProbeText = Get-Content -Raw -LiteralPath $playbackProbe
		$tokens = $null
		$parseErrors = $null
		$scriptAst = [System.Management.Automation.Language.Parser]::ParseFile(
			$verifyScript,
			[ref]$tokens,
			[ref]$parseErrors
		)
		$parseErrors.Count | Should Be 0
	}

	It 'uses actual public provider and direct-media URLs rather than synthetic playback data' {
		foreach ($id in @(
			'youtube', 'vimeo', 'twitch', 'streamable', 'dailymotion',
			'soundcloud', 'spotify', 'tiktok',
			'instagram', 'facebook', 'reddit-direct', 'x-direct', '4chan-direct'
		)) {
			$scriptText | Should Match ('id = "' + [regex]::Escape($id) + '"')
		}
		$scriptText | Should Match 'youtube-nocookie\.com/embed/M7lc1UVf-VE'
		$scriptText | Should Match 'player\.vimeo\.com/video/1195621748'
		$scriptText | Should Match 'clips\.twitch\.tv/embed\?clip='
		$scriptText | Should Match 'streamable\.com/e/ba9f2'
		$scriptText | Should Match 'geo\.dailymotion\.com/player\.html\?video=x84sh87'
		$scriptText | Should Match 'v\.redd\.it/.+CMAF_360\.mp4'
		$scriptText | Should Match 'video\.twimg\.com/.+\.mp4'
		$scriptText | Should Match 'i\.4cdn\.org/.+\.webm'
		$scriptText | Should Not Match 'data:audio/wav;base64'
	}

	It 'runs cold and warm passes in fresh client processes with one isolated provider-state root' {
		$scriptText | Should Match 'ValidateSet\("cold", "warm"\)'
		$scriptText | Should Match 'Invoke-RealMediaPass -MediaCase \$mediaCase -Pass "cold"'
		$scriptText | Should Match 'Invoke-RealMediaPass -MediaCase \$mediaCase -Pass "warm"'
		$scriptText | Should Match '\$process = Start-Process -FilePath \$executablePath'
		$scriptText | Should Match 'MUMBLE_MEDIA_PROFILE_ROOT = \$providerStateRoot'
		$scriptText | Should Match 'provider_state_reused'
		$scriptText | Should Match 'state_retained'
	}

	It 'requests real playback and requires observable playback evidence' {
		$scriptText | Should Match 'command = "watchTogetherAction"'
		$scriptText | Should Match 'action = "play"'
		$scriptText | Should Match 'maximumPosition - \$initialPosition -ge 0\.5'
		$scriptText | Should Match 'playbackVerified'
		$scriptText | Should Match 'playback_observed'
		$scriptText | Should Match 'PlaybackObservationSeconds'
		$scriptText | Should Match 'surfaceVerificationState'
		$scriptText | Should Match '"blocked", "failed"'
	}

	It 'measures the whole process tree and aborts runaway memory without imposing a tiny playback budget' {
		$scriptText | Should Match 'Get-DescendantProcessIds'
		$scriptText | Should Match 'WorkingSet64'
		$scriptText | Should Match 'PrivateMemorySize64'
		$scriptText | Should Match 'aggregate_cpu_percent'
		$scriptText | Should Match 'MemoryAbortBytes = 2GB'
		$scriptText | Should Match 'MemoryAbortBytes must be at least 256 MiB'
		$scriptText | Should Match 'Process tree exceeded memory guard'
		$scriptText | Should Match 'PostCloseObservationSeconds = 6'
		$scriptText | Should Match 'post_close_process_count'
	}

	It 'keeps media caches bounded in memory while allowing only provider state to persist' {
		$profileFactoryText | Should Match 'MemoryHttpCache'
		$profileFactoryText | Should Match 'VideoMemoryCacheBytes = 32 \* 1024 \* 1024'
		$profileFactoryText | Should Match 'AudioMemoryCacheBytes = 8 \* 1024 \* 1024'
		$profileFactoryText | Should Match 'AllowPersistentCookies'
		$profileFactoryText | Should Match 'media-provider-state'
		$profileFactoryText | Should Match 'releaseProfiles'
	}

	It 'recognizes provider consent controls in both document and known shadow roots' {
		$playbackProbeText | Should Match "consentRoots=\[document\]"
		$playbackProbeText | Should Match "querySelector\('tiktok-cookie-banner'\)"
		$playbackProbeText | Should Match 'tiktokConsent\.shadowRoot'
		$playbackProbeText | Should Match 'decline optional cookies'
	}

	It 'captures the stable final surface and gates teardown separately from provider availability' {
		$scriptText | Should Match 'command = "captureQml"'
		$scriptText | Should Match 'window = "media-session"'
		$scriptText | Should Match 'Wait-MediaClosed'
		$scriptText | Should Match 'surface_released'
		$scriptText | Should Match 'media_inactive'
		$scriptText | Should Match 'network_results_are_environment_dependent = \$true'
		$scriptText | Should Match 'contract_passed'
	}

	It 'creates a private config and never embeds credentials' {
		$scriptText | Should Match 'New-IsolatedConfig'
		$scriptText | Should Match 'auto_connect_to_last_server'
		$scriptText | Should Match 'check_for_plugin_updates'
		$scriptText | Should Match 'database_location'
		$scriptText | Should Not Match 'password|set-su-pw|SuperUser'
	}
}
