$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$verifyScript = Join-Path $repoRoot 'scripts\windows\verify-qml-real-media-playback.ps1'
$mainWindow = Join-Path $repoRoot 'src\mumble\MainWindow.cpp'
$profileFactory = Join-Path $repoRoot 'src\mumble\QmlMediaProfileFactory.cpp'
$playbackProbe = Join-Path $repoRoot 'src\mumble\qml-shell\MediaPlaybackProbe.js'

Describe 'Qt Quick real-media playback verifier' {
	BeforeAll {
		$scriptText = Get-Content -Raw -LiteralPath $verifyScript
		$mainWindowText = Get-Content -Raw -LiteralPath $mainWindow
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
		$scriptText | Should Match 'sourceUrl = "https://www\.youtube\.com/watch\?v=M7lc1UVf-VE"'
		$scriptText | Should Match 'sourceUrl = "https://vimeo\.com/1195621748"'
		$scriptText | Should Match 'sourceUrl = "https://clips\.twitch\.tv/IncredulousAbstemiousFennelImGlitch"'
		$scriptText | Should Match 'sourceUrl = "https://streamable\.com/ba9f2"'
		$scriptText | Should Match 'sourceUrl = "https://www\.dailymotion\.com/video/x84sh87"'
		$scriptText | Should Match 'sourceUrl = "https://soundcloud\.com/forss/flickermood"'
		$scriptText | Should Match 'sourceUrl = "https://open\.spotify\.com/album/2dFcS2u5YoUj4WmUkZ1oW6"'
		$scriptText | Should Match 'sourceUrl = "https://www\.tiktok\.com/@imee_2001/video/7611978857305984274"'
		$scriptText | Should Match 'sourceUrl = "https://www\.instagram\.com/reel/DYWnW2RMbWr/"'
		$scriptText | Should Match 'sourceUrl = "https://www\.facebook\.com/reel/1327313299204519"'
		$scriptText | Should Match 'sourceUrl = "https://www\.reddit\.com/r/interestingasfuck/comments/1tobuah/'
		$scriptText | Should Match 'sourceUrl = "https://x\.com/historyinmemes/status/2058971862265151767"'
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

	It 'persists concrete format branches without treating unmeasured cards as playback benchmarks' {
		foreach ($id in @(
			'instagram-static', 'reddit-still', 'reddit-gif', 'x-still',
			'x-animated-gif', 'giphy-gif', 'tenor-gif', 'imgur-gifv', 'tiktok-photo'
		)) {
			$scriptText | Should Match ('id = "' + [regex]::Escape($id) + '"')
		}
		$scriptText | Should Match 'source_url = \$MediaCase\.sourceUrl'
		$scriptText | Should Match 'playback_url = \$MediaCase\.playbackUrl'
		$scriptText | Should Match 'source_item_id = \$MediaCase\.sourceItemId'
		$scriptText | Should Match 'playback_item_id = \$MediaCase\.playbackItemId'
		$scriptText | Should Match 'content_branch = \$MediaCase\.contentBranch'
		$scriptText | Should Match 'expected_presentation = \$MediaCase\.expectedPresentation'
		$scriptText | Should Match 'verification_surface = \$MediaCase\.verificationSurface'
		$scriptText | Should Match 'format_coverage = @\(\$formatCoverage\)'
		$scriptText | Should Match 'coverage_state = "runtime-pending"'
		$scriptText | Should Match 'content_branch = "still-image"'
		$scriptText | Should Match 'content_branch = "animated-gif"'
		$scriptText | Should Match 'content_branch = "animated-gif-video-backed"'
		$scriptText | Should Match 'content_branch = "photo-carousel"'
		$scriptText | Should Match 'expected_presentation = "image-card"'
		$scriptText | Should Match 'expected_presentation = "animated-image"'
		$scriptText | Should Match 'expected_presentation = "provider-post-viewer"'
		$scriptText | Should Match 'https://www\.reddit\.com/r/animegifs/comments/g91mkj/'
		$scriptText | Should Match 'https://x\.com/FloodSocial/status/870042717589340160'
		$scriptText | Should Match 'https://giphy\.com/gifs/.+-xT4uQCfBOBGralHfOM'
		$scriptText | Should Match 'https://tenor\.com/view/what-is-this-gif-2935825949418015718'
		$scriptText | Should Match 'https://imgur\.com/owLfF25'
		$scriptText | Should Match 'https://www\.tiktok\.com/@contextify0/photo/7626953410033585428'
		$scriptText | Should Not Match 'sourceUrl = "https://(?:www\.)?(?:youtube|vimeo|reddit|tiktok|instagram|facebook)\.com/?"'
	}

	It 'keeps GIPHY GIFs on the managed animated-image path instead of video chrome' {
		$mainWindowText | Should Match 'https://media\.giphy\.com/media/%1/giphy\.gif'
		$mainWindowText | Should Match 'QStringLiteral\("image/gif"\)'
		$mainWindowText | Should Not Match 'https://media\.giphy\.com/media/%1/giphy\.mp4'
		$scriptText | Should Match 'playback_url = "https://media\.giphy\.com/media/xT4uQCfBOBGralHfOM/giphy\.gif"'
		$scriptText | Should Match 'expected_presentation = "animated-image"'
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
