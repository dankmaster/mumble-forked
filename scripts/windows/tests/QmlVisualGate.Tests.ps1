Import-Module "$PSScriptRoot\..\QmlVisualGate.Common.psm1" -Force

function Get-QmlVisualCaseValue {
	param(
		[Parameter(Mandatory = $true)]$Case,
		[Parameter(Mandatory = $true)][string]$Name,
		$Default = $null
	)
	$property = $Case.PSObject.Properties[$Name]
	if ($null -eq $property) { return $Default }
	return $property.Value
}

Describe "Qt Quick visual PNG tolerance" {
	BeforeAll {
		Add-Type -AssemblyName System.Drawing
		$script:pngRoot = Join-Path ([IO.Path]::GetTempPath()) ("qml-visual-png-" + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $script:pngRoot | Out-Null
		function New-TestPng {
			param([string]$Path, [hashtable]$ChangedPixels = @{})
			$bitmap = [Drawing.Bitmap]::new(32, 32, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
			try {
				$base = [Drawing.Color]::FromArgb(255, 40, 50, 60)
				for ($y = 0; $y -lt 32; ++$y) { for ($x = 0; $x -lt 32; ++$x) { $bitmap.SetPixel($x, $y, $base) } }
				foreach ($entry in $ChangedPixels.GetEnumerator()) {
					$parts = $entry.Key -split ','
					$bitmap.SetPixel([int]$parts[0], [int]$parts[1], $entry.Value)
				}
				$bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
			} finally { $bitmap.Dispose() }
		}
		$script:baselinePng = Join-Path $script:pngRoot 'baseline.png'
		New-TestPng $script:baselinePng
	}

	AfterAll { Remove-Item -LiteralPath $script:pngRoot -Recurse -Force }

	It "passes identical PNG hashes without pixel drift" {
		$result = Compare-QmlVisualPng $script:baselinePng $script:baselinePng
		$result.passed | Should Be $true
		$result.exact | Should Be $true
		$result.changed_pixels | Should Be 0
	}

	It "allows three low-delta changed pixels" {
		$candidate = Join-Path $script:pngRoot 'three-pixels.png'
		$changes = @{
			'0,0' = [Drawing.Color]::FromArgb(255, 50, 50, 60)
			'1,0' = [Drawing.Color]::FromArgb(255, 40, 60, 60)
			'2,0' = [Drawing.Color]::FromArgb(255, 40, 50, 70)
		}
		New-TestPng $candidate $changes
		$result = Compare-QmlVisualPng $script:baselinePng $candidate
		$result.passed | Should Be $true
		$result.exact | Should Be $false
		$result.changed_pixels | Should Be 3
		$result.maximum_channel_delta | Should Be 10
	}

	It "rejects more than the minimum pixel allowance" {
		$candidate = Join-Path $script:pngRoot 'seventeen-pixels.png'
		$changes = @{}; foreach ($x in 0..16) { $changes["$x,0"] = [Drawing.Color]::FromArgb(255, 41, 50, 60) }
		New-TestPng $candidate $changes
		$result = Compare-QmlVisualPng $script:baselinePng $candidate
		$result.allowed_changed_pixels | Should Be 16
		$result.changed_pixels | Should Be 17
		$result.passed | Should Be $false
	}

	It "rejects a channel delta above 32 even for one pixel" {
		$candidate = Join-Path $script:pngRoot 'large-delta.png'
		New-TestPng $candidate @{ '0,0' = [Drawing.Color]::FromArgb(255, 73, 50, 60) }
		$result = Compare-QmlVisualPng $script:baselinePng $candidate
		$result.changed_pixels | Should Be 1
		$result.maximum_channel_delta | Should Be 33
		$result.passed | Should Be $false
	}

	It "rejects a black frame patch" {
		$candidate = Join-Path $script:pngRoot 'black-patch.png'
		$changes = @{}
		foreach ($y in 0..4) { foreach ($x in 0..3) { $changes["$x,$y"] = [Drawing.Color]::Black } }
		New-TestPng $candidate $changes
		$result = Compare-QmlVisualPng $script:baselinePng $candidate
		$result.changed_pixels | Should Be 20
		$result.passed | Should Be $false
	}

	It "measures non-black frame coverage independently of a baseline" {
		$healthy = Get-QmlVisualPngCoverage $script:baselinePng
		$healthy.non_black_fraction | Should Be 1.0

		$black = Join-Path $script:pngRoot 'black-frame.png'
		$changes = @{}
		foreach ($y in 0..31) {
			foreach ($x in 0..31) { $changes["$x,$y"] = [Drawing.Color]::Black }
		}
		New-TestPng $black $changes
		$coverage = Get-QmlVisualPngCoverage $black
		$coverage.non_black_pixels | Should Be 0
		$coverage.non_black_fraction | Should Be 0.0
	}

	It "rejects a stable partial frame whose global coverage hides a black region" {
		$partial = Join-Path $script:pngRoot 'black-right-half.png'
		$changes = @{}
		foreach ($y in 0..31) {
			foreach ($x in 16..31) { $changes["$x,$y"] = [Drawing.Color]::Black }
		}
		New-TestPng $partial $changes
		$global = Get-QmlVisualPngCoverage $partial
		$grid = Get-QmlVisualPngGridCoverage -Path $partial -Columns 2 -Rows 1
		$global.non_black_fraction | Should Be 0.5
		$grid.cell_non_black_fractions.Count | Should Be 2
		$grid.cell_non_black_fractions[0] | Should Be 1.0
		$grid.cell_non_black_fractions[1] | Should Be 0.0
		$grid.minimum_non_black_fraction | Should Be 0.0
	}

	It "fails closed for a corrupt PNG" {
		$candidate = Join-Path $script:pngRoot 'corrupt.png'
		[IO.File]::WriteAllBytes($candidate, [byte[]](1, 2, 3, 4))
		$threw = $false
		try { Compare-QmlVisualPng $script:baselinePng $candidate | Out-Null } catch { $threw = $true }
		$threw | Should Be $true
	}
}

Describe "Qt Quick visual manifest validation" {
	It "accepts a complete manifest" {
		$manifest = [pscustomobject]@{ schema_version = 1; cases = @([pscustomobject]@{
			id = "desktop"; image_sha256 = "a" * 64; accessibility_sha256 = "b" * 64
			image_width = 1280; image_height = 800
		}) }
		Assert-QmlVisualManifest $manifest | Should Be $true
	}

	It "rejects duplicate cases" {
		$case = [pscustomobject]@{ id = "same"; image_sha256 = "a" * 64; accessibility_sha256 = "b" * 64; image_width = 1; image_height = 1 }
		$threw = $false
		try { Assert-QmlVisualManifest ([pscustomobject]@{ schema_version = 1; cases = @($case, $case) }) | Out-Null }
		catch { $threw = $true }
		$threw | Should Be $true
	}

	It "rejects missing accessibility evidence" {
		$case = [pscustomobject]@{ id = "case"; image_sha256 = "a" * 64; accessibility_sha256 = ""; image_width = 1; image_height = 1 }
		$threw = $false
		try { Assert-QmlVisualManifest ([pscustomobject]@{ schema_version = 1; cases = @($case) }) | Out-Null }
		catch { $threw = $true }
		$threw | Should Be $true
	}

	It "keeps the checked-in matrix partitionable by process DPR" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$groups = @($matrix.cases | Group-Object device_pixel_ratio)
		$groups.Count | Should Be 2
		@($groups | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum | Should Be @($matrix.cases).Count
	}

	It "covers the real compact breakpoint with both drawer states" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$compact = @($matrix.cases | Where-Object layout -eq "compact")
		$compact.Count | Should BeGreaterThan 1
		@($compact | Where-Object {
			[int]$_.width -lt 900 -and -not [bool](Get-QmlVisualCaseValue $_ "navigation_open" $false)
		}).Count |
			Should BeGreaterThan 0
		@($compact | Where-Object {
			[int]$_.width -eq 420 -and [bool](Get-QmlVisualCaseValue $_ "navigation_open" $false)
		}).Count |
			Should BeGreaterThan 0
		foreach ($contract in @(
			@{ id = "compact-420-closed"; width = 420; height = 700; open = $false },
			@{ id = "compact-760-open"; width = 760; height = 700; open = $true },
			@{ id = "compact-420x520-open"; width = 420; height = 520; open = $true }
		)) {
			$case = @($matrix.cases | Where-Object { [string]$_.id -eq $contract.id })
			$case.Count | Should Be 1
			[int]$case[0].width | Should Be $contract.width
			[int]$case[0].height | Should Be $contract.height
			[bool]$case[0].navigation_open | Should Be $contract.open
		}
		$below = @($matrix.cases | Where-Object { [string]$_.id -eq "breakpoint-899-compact" })
		$at = @($matrix.cases | Where-Object { [string]$_.id -eq "breakpoint-900-regular" })
		$below.Count | Should Be 1
		$at.Count | Should Be 1
		[int]$below[0].width | Should Be 899
		[string]$below[0].layout | Should Be "compact"
		[int]$at[0].width | Should Be 900
		[string]$at[0].layout | Should Be "regular"
	}

	It "keeps the original eleven visual cases unchanged" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$originalIds = @(
			"desktop-light-connected", "desktop-dark-connected", "hidpi-dark-connected",
			"compact-light-connected", "compact-dark-connected", "desktop-light-empty",
			"desktop-dark-empty", "desktop-light-loading", "desktop-dark-loading",
			"desktop-light-error", "desktop-dark-error"
		)
		$originalCases = @($matrix.cases | Where-Object { [string]$_.id -in $originalIds })
		$originalCases.Count | Should Be 11
		foreach ($case in $originalCases) {
			@($case.PSObject.Properties.Name | Where-Object { $_ -eq "motd_variant" }).Count | Should Be 0
			@($case.PSObject.Properties.Name | Where-Object { $_ -eq "rich_preview_variant" }).Count | Should Be 0
		}
	}

	It "preserves every pre-expansion visual case id" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$existingIds = @(
			"desktop-light-connected", "desktop-dark-connected", "hidpi-dark-connected",
			"compact-light-connected", "compact-dark-connected", "desktop-light-empty",
			"desktop-dark-empty", "desktop-light-loading", "desktop-dark-loading",
			"desktop-light-error", "desktop-dark-error", "motd-light-expanded",
			"motd-dark-collapsed", "motd-light-changed", "motd-dark-history-visible",
			"motd-compact-short-expanded", "preview-dark-youtube", "preview-light-youtube",
			"preview-hidpi-dark-youtube", "preview-dark-spotify", "preview-light-tiktok",
			"preview-dark-instagram", "preview-light-finance", "preview-dark-audio",
			"preview-light-product", "preview-compact-youtube", "preview-large-product",
			"preview-dark-steam", "preview-light-google-search", "preview-dark-twitch",
			"preview-light-flashback", "preview-dark-loading", "preview-light-error",
			"preview-custom-youtube"
		)
		foreach ($id in $existingIds) {
			@($matrix.cases | Where-Object { [string]$_.id -eq $id }).Count | Should Be 1
		}
	}

	It "covers every deterministic MOTD surface and a constrained compact height" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$motdCases = @($matrix.cases | Where-Object { $_.PSObject.Properties.Name -contains "motd_variant" })
		$motdCases.Count | Should Be 5
		@($motdCases | Where-Object { [string]$_.state -ne "connected" }).Count | Should Be 0
		foreach ($variant in @("expanded", "collapsed", "changed", "history-visible")) {
			@($motdCases | Where-Object { [string]$_.motd_variant -eq $variant }).Count | Should BeGreaterThan 0
		}
		@($motdCases | Where-Object {
			[string]$_.motd_variant -eq "expanded" -and [string]$_.layout -eq "compact" -and
			[int]$_.width -lt 900 -and [int]$_.height -le 560
		}).Count | Should Be 1
	}

	It "covers representative rich-preview families, embed aspects, and both themes" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$previewCases = @($matrix.cases | Where-Object {
			$_.PSObject.Properties.Name -contains "rich_preview_variant" -and
			-not ($_.PSObject.Properties.Name -contains "surface_variant")
		})
		$previewCases.Count | Should Be 63
		@($previewCases | Where-Object { [string]$_.state -ne "connected" }).Count | Should Be 0
		foreach ($variant in @(
			"youtube", "spotify", "tiktok", "instagram", "finance", "audio", "product",
			"steam", "google", "twitch", "flashback", "x", "github", "social",
			"vehicle", "property", "marketplace", "article", "weather", "place", "traffic",
			"link-digest", "vimeo", "dailymotion", "soundcloud", "sensitive", "direct-media",
			"loading", "error"
		)) {
			@($previewCases | Where-Object { [string]$_.rich_preview_variant -eq $variant }).Count |
				Should BeGreaterThan 0
		}
		$themePairedVariants = @(
			"youtube", "spotify", "tiktok", "vimeo", "dailymotion", "soundcloud", "instagram",
			"finance", "audio", "product", "steam", "google", "twitch", "flashback", "x",
			"github", "social", "vehicle", "property", "marketplace", "article", "weather",
			"place", "traffic", "link-digest", "sensitive", "direct-media", "loading", "error"
		)
		foreach ($variant in $themePairedVariants) {
			$themes = @($previewCases | Where-Object {
				[string]$_.rich_preview_variant -eq $variant
			} | ForEach-Object { [string]$_.theme } | Sort-Object -Unique)
			($themes -contains "light") | Should Be $true
			($themes -contains "dark") | Should Be $true
		}
		@($previewCases | Where-Object { [string]$_.theme -eq "light" }).Count | Should BeGreaterThan 0
		@($previewCases | Where-Object { [string]$_.theme -eq "dark" }).Count | Should BeGreaterThan 0
		@($previewCases | Where-Object { [string]$_.theme -eq "custom" }).Count | Should Be 1
		@($previewCases | Where-Object {
			[double]$_.device_pixel_ratio -eq 1.5 -and [string]$_.rich_preview_variant -eq "youtube" -and
			[string]$_.theme -eq "dark" -and [int]$_.width -eq 960 -and [int]$_.height -eq 680 -and
			([int]$_.height * [double]$_.device_pixel_ratio) -le 1020
		}).Count | Should Be 1
		@($previewCases | Where-Object {
			[string]$_.layout -eq "compact" -and [int]$_.width -lt 900 -and
			[string](Get-QmlVisualCaseValue $_ "rich_preview_size" "") -eq "compact"
		}).Count | Should Be 2
		@($previewCases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "rich_preview_size" "") -eq "large"
		}).Count | Should Be 2
	}

	It "keeps fixture windows within a common Windows desktop work area" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		@($matrix.cases | Where-Object { [int]$_.height -gt 1000 }).Count | Should Be 0
		$largeCases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "rich_preview_size" "") -eq "large"
		})
		$largeCases.Count | Should Be 2
		@($largeCases | Where-Object { [int]$_.height -eq 1000 }).Count | Should Be 2
	}

	It "pairs every presentation family across light and dark deterministic cases" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$presentationCases = @($matrix.cases | Where-Object {
			-not ($_.PSObject.Properties.Name -contains "surface_variant") -and
			($_.PSObject.Properties.Name -contains "presentation_family" -or
			 $_.PSObject.Properties.Name -contains "case_variant")
		})
		$presentationCases.Count | Should Be 65
		foreach ($case in $presentationCases) {
			($case.PSObject.Properties.Name -contains "presentation_family") | Should Be $true
			($case.PSObject.Properties.Name -contains "case_variant") | Should Be $true
			[string]$case.presentation_family | Should Not BeNullOrEmpty
			[string]$case.case_variant | Should Not BeNullOrEmpty
			[string]$case.state | Should Be "connected"
		}
		$families = @($presentationCases | Group-Object presentation_family)
		@($families.Name | Sort-Object) | Should Be @(
			"commerce", "details", "embed", "generic", "identity", "market", "media",
			"message-body", "state"
		)
		foreach ($family in $families) {
			$themes = @($family.Group | ForEach-Object { [string]$_.theme } | Sort-Object -Unique)
			($themes -contains "light") | Should Be $true
			($themes -contains "dark") | Should Be $true
		}
	}

	It "covers the audited rich families, media states, message image link, and both size modes" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		foreach ($variant in @("x", "github", "social", "vehicle", "property", "article",
			"sensitive", "direct-media", "rich-image-link")) {
			@($matrix.cases | Where-Object {
				[string](Get-QmlVisualCaseValue $_ "case_variant" "") -eq $variant
			}).Count |
				Should BeGreaterThan 0
		}
		foreach ($size in @("compact", "large")) {
			$cases = @($matrix.cases | Where-Object {
				[string](Get-QmlVisualCaseValue $_ "rich_preview_size" "") -eq $size
			})
			@($cases | Where-Object { [string]$_.theme -eq "light" }).Count | Should Be 1
			@($cases | Where-Object { [string]$_.theme -eq "dark" }).Count | Should Be 1
		}
		$richImageCases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "case_variant" "") -eq "rich-image-link"
		})
		$richImageCases.Count | Should Be 2
		@($richImageCases | Where-Object {
			$_.PSObject.Properties.Name -contains "rich_preview_variant"
		}).Count | Should Be 0
	}

	It "covers every deterministic native product surface with explicit viewer theme pairs" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$expectedSurfaces = @(
			"settings-audio-input", "settings-audio-input-advanced",
			"settings-audio-output", "settings-audio-output-advanced", "settings-appearance",
			"settings-user-interface", "settings-messages-sounds", "settings-messages-events",
			"settings-messages-events-compact",
			"settings-key-bindings", "settings-key-bindings-populated",
			"settings-network", "settings-network-advanced", "settings-screen-sharing",
			"settings-plugins", "settings-plugins-updating", "settings-plugins-partial",
			"settings-about", "dialog-connect", "dialog-connect-editor",
			"dialog-connect-validation", "dialog-connect-empty",
			"dialog-search-empty", "dialog-search-results", "dialog-search-regex-error",
			"dialog-certificate", "dialog-certificate-create", "dialog-acl-populated", "dialog-stonks-populated",
			"dialog-recorder", "dialog-recorder-recording",
			"dialog-server-users-loading", "dialog-server-users-ready", "dialog-server-users-edit",
			"dialog-server-users-confirm", "dialog-server-bans-empty", "dialog-server-bans-edit",
			"dialog-server-bans-error",
			"menu-app", "menu-app-server", "menu-profile", "menu-room", "menu-text-room",
			"menu-participant", "menu-chat-background", "menu-message",
			"chat-message-states", "chat-composer-states", "chat-attachment-states",
			"chat-history-prepend-anchor",
			"conversation-search-match", "conversation-search-empty",
			"direct-message-main", "direct-message-tray", "direct-message-window",
			"attachment-viewer", "image-viewer",
			"screen-share-editor", "screen-share-editor-compact", "screen-share-view-loading", "screen-share-view-error",
			"screen-share-view-active", "screen-share-view-paused", "manual-plugin", "ptt-idle", "ptt-active",
			"async-running", "async-error", "async-success", "update-banner", "watch-together-hosting",
			"toast-single", "toast-duplicate",
			"media-inline-loading", "media-inline-active", "media-inline-error",
			"media-inline-retry", "media-inline-external", "media-inline-controls"
		)
		$surfaceCases = @($matrix.cases | Where-Object {
			$_.PSObject.Properties.Name -contains "surface_variant"
		})
		$pairedSurfaces = @("attachment-viewer", "image-viewer", "screen-share-view-active")
		@($matrix.cases).Count | Should Be 168
		$surfaceCases.Count | Should Be ($expectedSurfaces.Count + $pairedSurfaces.Count)
		@($surfaceCases.surface_variant | Sort-Object -Unique) | Should Be @($expectedSurfaces | Sort-Object)
		foreach ($surface in $expectedSurfaces) {
			$matches = @($surfaceCases | Where-Object { [string]$_.surface_variant -eq $surface })
			$expectedCount = if ($surface -in $pairedSurfaces) { 2 } else { 1 }
			$matches.Count | Should Be $expectedCount
		}
		foreach ($surface in $pairedSurfaces) {
			$matches = @($surfaceCases | Where-Object { [string]$_.surface_variant -eq $surface })
			@($matches | Where-Object { [string]$_.theme -eq "light" }).Count | Should Be 1
			@($matches | Where-Object { [string]$_.theme -eq "dark" }).Count | Should Be 1
		}
		@($surfaceCases | Where-Object {
			[string]$_.state -ne "connected" `
				-and [string]$_.surface_variant -notlike "dialog-connect*" `
				-and [string]$_.surface_variant -notlike "dialog-certificate*"
		}).Count | Should Be 0
	}

	It "requires the provider-specific accessible identity for inline YouTube media" {
		$gateScript = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		foreach ($surface in @("loading", "active", "error", "retry", "external", "controls")) {
			$gateScript | Should Match ('"media-inline-{0}" \{{ @\("YouTube inline media player",' -f $surface)
		}
		$gateScript | Should Not Match '"media-inline-[^"]+" \{ @\("Inline media player",'
	}

	It "gates inline provider failure surfaces by the product's provider-specific error title" {
		$gateScript = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		foreach ($surface in @("media-inline-error", "media-inline-retry", "media-inline-external")) {
			$gateScript | Should Match ('"{0}" \{{ @\([^\r\n]*"YouTube playback failed"' -f $surface)
		}
		$gateScript | Should Not Match '"media-inline-(?:error|retry|external)" \{ @\([^\r\n]*"Media playback failed"'
	}

	It "excludes detached media surfaces that the Windows product disables for render-loop safety" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		@($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "media-detached-*"
		}).Count | Should Be 0
		$models = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlClientModels.cpp"
		$models | Should Match '(?s)bool MediaSessionBackend::detachedPlaybackSupported\(\) const \{\s*#ifdef Q_OS_WIN.*?return false;'
	}

	It "gates attachment image previews by their exact accessible alt label" {
		$gateScript = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$gateScript | Should Match '"chat-attachment-states" \{ @\([\s\S]*?"Community test dashboard", "Loading Performance trace"'
		$gateScript | Should Not Match '"View preview Community test dashboard"'
	}

	It "gates the production Connect empty-state copy and count" {
		$gateScript = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$gateScript | Should Match '"dialog-connect-empty" \{ @\('
		$gateScript | Should Match '"Saved servers", "Favorites", "0 server\(s\)", "Add a server to get started\."'
		$gateScript | Should Not Match '"No saved servers yet"'
	}

	It "covers integrated message composer attachment and history-prepend states fail closed" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$expectedCases = @{
			"surface-light-chat-message-states" = @{
				surface = "chat-message-states"; width = 1280; height = 860
				theme = "light"; layout = "regular"; density = "comfortable"
			}
			"surface-compact-dark-chat-composer-states" = @{
				surface = "chat-composer-states"; width = 760; height = 900
				theme = "dark"; layout = "compact"; density = "compact"
			}
			"surface-light-chat-attachment-states" = @{
				surface = "chat-attachment-states"; width = 1280; height = 900
				theme = "light"; layout = "regular"; density = "comfortable"
			}
			"surface-dark-chat-history-prepend-anchor" = @{
				surface = "chat-history-prepend-anchor"; width = 1280; height = 800
				theme = "dark"; layout = "regular"; density = "comfortable"
			}
		}
		$chatCases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "chat-*"
		})
		$chatCases.Count | Should Be 4
		foreach ($entry in $expectedCases.GetEnumerator()) {
			$matches = @($chatCases | Where-Object { [string]$_.id -eq $entry.Key })
			$matches.Count | Should Be 1
			$case = $matches[0]
			[string]$case.surface_variant | Should Be $entry.Value.surface
			[string]$case.state | Should Be "connected"
			[int]$case.width | Should Be $entry.Value.width
			[int]$case.height | Should Be $entry.Value.height
			[string]$case.theme | Should Be $entry.Value.theme
			[string]$case.layout | Should Be $entry.Value.layout
			$density = if ($case.PSObject.Properties.Name -contains "density") {
				[string]$case.density
			} elseif ([string]$case.layout -eq "compact") { "compact" } else { "comfortable" }
			$density | Should Be $entry.Value.density
		}

		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		foreach ($surface in @(
			"chat-message-states", "chat-composer-states", "chat-attachment-states",
			"chat-history-prepend-anchor")) {
			$controller | Should Match ([regex]::Escape('QStringLiteral("' + $surface + '")'))
			$worker | Should Match ([regex]::Escape('"' + $surface + '"'))
		}
		$controller | Should Match 'ComposerController \*composer = m_host->composerController\(\)'
		$controller | Should Match 'chat->replaceMessages\(fixtureMessages\)'
		$controller | Should Match 'm_host->chatModel\(\)->replaceMessages\(prependedMessages\)'
		$controller | Should Match 'anchorAfterRow == anchorBeforeRow \+ VisualHistoryPrependMessageCount'
		$controller | Should Match 'visualFixtureSurfaceVariant'
		$controller | Should Match 'QStringLiteral\("chat_fixture_state"\)'
		$worker | Should Match 'requiredChatStateProperties'
		$worker | Should Match 'expectedNumericState'
		$worker | Should Match 'composer_upload_progress_percent'
		$worker | Should Match 'anchor_before_row\s*-ne\s*11'
		$worker | Should Match 'anchor_after_row\s*-ne\s*17'
		$worker | Should Match 'reportedOffsetDelta\s*-gt\s*1\.0'
		$worker | Should Match 'did not execute a stable six-row production prepend'
		$worker | Should Match '"chat-history-prepend-anchor"\s*\{\s*@\('
		$worker | Should Match 'command\s*=\s*"qmlTimelinePresentationState"'
		$worker | Should Match 'presentationPending'
		$worker | Should Match 'presentationFinalizing'
		$worker | Should Match 'scopeResetPending'
		$worker | Should Match 'did not finish its production timeline presentation'
	}

	It "covers model-backed profile text-room and chat-background menus" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$expectedCases = @{
			"surface-dark-menu-profile" = "menu-profile"
			"surface-light-menu-text-room" = "menu-text-room"
			"surface-dark-menu-chat-background" = "menu-chat-background"
		}
		foreach ($entry in $expectedCases.GetEnumerator()) {
			$matches = @($matrix.cases | Where-Object {
				[string]$_.id -eq $entry.Key -and
					[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -eq $entry.Value
			})
			$matches.Count | Should Be 1
			[string]$matches[0].state | Should Be "connected"
		}

		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$controller | Should Match 'session->setSelfMenu\('
		$controller | Should Match 'QStringLiteral\("textRoom"\)'
		$controller | Should Match 'QStringLiteral\("chatBackground"\)'
		$mainQml | Should Match 'openProfileMenu\('
		$mainQml | Should Match 'openScopeMenu\(row\.scopeToken'
		$mainQml | Should Match 'openChatBackgroundMenu\('
		foreach ($surface in $expectedCases.Values) {
			$worker | Should Match ([regex]::Escape('"' + $surface + '"'))
		}
		$worker | Should Match '"menu-profile"\s*\{\s*@\("Demo User",\s*"Presence",\s*"Profile"\)'
		$worker | Should Match 'Profile menu.*must expose Online once as the Demo User header description'
	}

	It "covers production controller-backed Connect states across theme compact and HiDPI" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$connectCases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "dialog-connect*"
		})
		@($connectCases.surface_variant | Sort-Object) | Should Be @(
			"dialog-connect", "dialog-connect-editor", "dialog-connect-empty", "dialog-connect-validation"
		)
		@($connectCases | Where-Object { [string]$_.theme -eq "light" }).Count | Should BeGreaterThan 0
		@($connectCases | Where-Object { [string]$_.theme -eq "dark" }).Count | Should BeGreaterThan 0
		@($connectCases | Where-Object {
			[string]$_.layout -eq "compact" -and
				[string](Get-QmlVisualCaseValue $_ "density" "") -eq "compact"
		}).Count | Should Be 1
		@($connectCases | Where-Object { [double]$_.device_pixel_ratio -gt 1 }).Count | Should Be 1
		@($connectCases | Where-Object { [string]$_.state -ne "empty" }).Count | Should Be 0

		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$connectController = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernConnectController.cpp"
		$connectFixture = [regex]::Match(
			$controller,
			'(?s)QVariantMap visualConnectDialog\(const QString &variant\)\s*\{.*?(?=\n\s*QVariantMap visualSearchDialog\()')
		$connectFixture.Success | Should Be $true
		($controller -match '#include "ModernConnectController\.h"') | Should Be $true
		($controller -match 'QVariantMap visualConnectDialog\(const QString &variant\)') | Should Be $true
		($controller -match 'ModernConnectController controller') | Should Be $true
		($controller -match 'controller\.setFavoritePing\(community\.qsHostname') | Should Be $true
		($controller -match 'controller\.invokeAction\(QStringLiteral\("editFavorite"\)') | Should Be $true
		($controller -match 'controller\.invokeAction\(QStringLiteral\("newFavorite"\)') | Should Be $true
		($connectController -match 'QStringLiteral\("width"\), 860') | Should Be $true
		($connectController -match 'QStringLiteral\("height"\), 640') | Should Be $true
		($connectController -match 'QStringLiteral\("primaryActionId"\), QStringLiteral\("connect"\)') | Should Be $true
		($connectFixture.Value -match 'QStringLiteral\("preferredWidth"\)') | Should Be $false
		($connectFixture.Value -match 'QStringLiteral\("preferredHeight"\)') | Should Be $false
	}

	It "gates high-risk community parity surfaces with deterministic native fixtures" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$expected = @{
			"dialog-acl-populated" = "surface-dark-acl-populated"
			"dialog-stonks-populated" = "surface-light-stonks-populated"
			"watch-together-hosting" = "surface-dark-watch-together-hosting"
			"direct-message-tray" = "surface-light-direct-message-tray"
		}
		foreach ($entry in $expected.GetEnumerator()) {
			$matches = @($matrix.cases | Where-Object {
				[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -eq $entry.Key -and
					[string]$_.id -eq $entry.Value
			})
			$matches.Count | Should Be 1
			[string]$matches[0].state | Should Be "connected"
			$controller | Should Match ([regex]::Escape('QStringLiteral("' + $entry.Key + '")'))
			$worker | Should Match ([regex]::Escape('"' + $entry.Key + '"'))
		}
		$controller | Should Match 'QStringLiteral\("aclEditor"\)'
		$controller | Should Match 'QStringLiteral\("kind"\), QStringLiteral\("stonks"\)'
		$controller | Should Match '(?s)QVariantMap visualStonksDialog\(\).*QStringLiteral\("accessSupported"\), true.*QStringLiteral\("allowed"\), true'
		$controller | Should Match 'applySharedState\('
		$controller | Should Match 'sharedHost\(\)'
		$controller | Should Match 'QStringLiteral\("direct-message-attachment"\)'
		$controller | Should Match 'QStringLiteral\("trayOpen"\), traySurface'
		$controller | Should Match 'visualMenuAction\(QStringLiteral\("acl"\), QStringLiteral\("Room access & settings\.\.\."\)'
		$worker | Should Match 'Watch Together: Community release watch party'
		$worker | Should Match 'Inherit access rules from parent room'
		$worker | Should Match 'Changes apply to Root / Lobby \(room ID 1\)'
		$worker | Should Match 'These permissions belong to the target room shown above\.'
		$worker | Should Match '"Rule for all"'
		$worker | Should Match 'Portfolio data is ready\.'
		$worker | Should Match 'Message attachments'
		$worker | Should Match '"menu-room"\s*\{\s*@\("Send room message…", "Copy room URL", "Room access & settings\.\.\."'
	}

	It "covers editable registered-user and ban-list states through the typed admin controllers" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$expected = @(
			"dialog-server-users-loading", "dialog-server-users-ready", "dialog-server-users-edit",
			"dialog-server-users-confirm", "dialog-server-bans-empty", "dialog-server-bans-edit",
			"dialog-server-bans-error"
		)
		$cases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "dialog-server-*"
		})
		@($cases.surface_variant | Sort-Object) | Should Be @($expected | Sort-Object)
		@($cases | Where-Object { [string]$_.state -ne "connected" }).Count | Should Be 0
		@($cases | Where-Object { [string]$_.theme -eq "light" }).Count | Should BeGreaterThan 0
		@($cases | Where-Object { [string]$_.theme -eq "dark" }).Count | Should BeGreaterThan 0

		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$editor = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ServerAdminEditor.qml"
		$controller | Should Match '#include "ModernServerAdminController\.h"'
		$controller | Should Match 'ModernRegisteredUsersController \*controller'
		$controller | Should Match 'ModernBanListController \*controller'
		$controller | Should Match 'controller->beginRename\('
		$editor | Should Match 'readonly property Item initialFocusTarget: searchField'
		$editor | Should Match 'id: confirmationLayer'
		$editor | Should Match 'KeyNavigation\.tab: confirmationCancelButton'
	}

	It "covers Search empty results and regex-error states through the production dialog schema" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$expected = @(
			"dialog-search-empty", "dialog-search-results", "dialog-search-regex-error"
		)
		$searchCases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "dialog-search-*"
		})
		@($searchCases.surface_variant | Sort-Object) | Should Be @($expected | Sort-Object)
		@($searchCases | Where-Object { [string]$_.state -ne "connected" }).Count | Should Be 0
		@($searchCases | Where-Object {
			[string]$_.layout -eq "compact" -and
				[string](Get-QmlVisualCaseValue $_ "density" "") -eq "compact"
		}).Count | Should Be 1

		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$controller | Should Match 'QVariantMap visualSearchDialog\(const QString &variant\)'
		$controller | Should Match 'QStringLiteral\("liveUpdate"\), true'
		$controller | Should Match 'QStringLiteral\("updateDelayMs"\), 140'
		$controller | Should Match 'QStringLiteral\("initialFocusId"\), QStringLiteral\("search\.query"\)'
		$controller | Should Match 'QObject::tr\("Invalid regular expression\."\)'
	}

	It "rejects a Connect favorite row that drifts from the production semantic contract" {
		$workerPath = "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$tokens = $null
		$parseErrors = $null
		$ast = [Management.Automation.Language.Parser]::ParseFile(
			(Resolve-Path $workerPath).Path, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		foreach ($functionName in @(
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityEntries", "Get-QmlAccessibilityFocusSummary",
			"Test-QmlAccessibilityPathContains", "Test-QmlAccessibilityPositiveRect",
			"Assert-QmlAccessibilityModalSubtree", "Assert-QmlAccessibilityHeadingLayout",
			"Assert-QmlAccessibilityViewportBounds", "Assert-QmlAccessibilityEvidence"
		)) {
			$functionAst = $ast.Find({
				param($node)
				$node -is [Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq $functionName
			}, $true)
			$null -eq $functionAst | Should Be $false
			. ([scriptblock]::Create($functionAst.Extent.Text))
		}

		$communityRow = [pscustomobject]@{
			role = "ListItem"; name = "Mumble Community"
			description = "voice.example.invalid:64738 / Demo User"
			states = @("selected"); rect = [pscustomobject]@{ x = 10; y = 40; width = 300; height = 60 }
			children = @()
		}
		$studioRow = [pscustomobject]@{
			role = "ListItem"; name = "Studio"
			description = "studio.example.invalid:64739 / Producer"
			states = @(); rect = [pscustomobject]@{ x = 10; y = 105; width = 300; height = 60 }
			children = @()
		}
		$requiredNames = @(
			"Connect to a server", "Saved servers", "Users: 18/128", "Ping: 28 ms",
			"Cancel", "Edit", "Connect"
		)
		$dialogChildren = @($communityRow, $studioRow) + @($requiredNames | ForEach-Object {
				[pscustomobject]@{ role = "StaticText"; name = $_; description = ""; states = @()
					rect = [pscustomobject]@{ x = 0; y = 0; width = 1; height = 1 }; children = @() }
			})
		$snapshot = [pscustomobject]@{
			role = "Window"; name = "Fixture shell"; description = ""; states = @("focused")
			rect = [pscustomobject]@{ x = 0; y = 0; width = 1100; height = 800 }
			children = @([pscustomobject]@{
				role = "Dialog"; name = "Connect to a server"; description = ""; states = @()
				rect = [pscustomobject]@{ x = 0; y = 0; width = 1100; height = 800 }
				children = $dialogChildren
			})
		}

		$thrown = $null
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $snapshot -CaseId "connect-contract" -State "empty" `
				-SurfaceVariant "dialog-connect"
		} catch { $thrown = $_ }
		$null -eq $thrown | Should Be $true

		$communityRow.description = ""
		$thrown = $null
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $snapshot -CaseId "connect-contract" -State "empty" `
				-SurfaceVariant "dialog-connect"
		} catch { $thrown = $_ }
		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match "production-shaped 'Mumble Community' favorite row"
	}

	It "runs a real spacious-density Settings fixture" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$spacious = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "density" "") -eq "spacious" -and
				[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "settings-*"
		})
		$spacious.Count | Should BeGreaterThan 0
		@($spacious | Where-Object { [string]$_.layout -ne "regular" }).Count | Should Be 0
	}

	It "covers every production Settings page through real controller-backed fixtures" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$settingsCases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "settings-*"
		})
		$productionSurfaces = @(
			"settings-audio-input", "settings-audio-output", "settings-appearance",
			"settings-user-interface", "settings-messages-sounds", "settings-key-bindings",
			"settings-network", "settings-screen-sharing", "settings-plugins", "settings-about"
		)
		foreach ($surface in $productionSurfaces) {
			@($settingsCases | Where-Object { [string]$_.surface_variant -eq $surface }).Count | Should Be 1
		}
		@($settingsCases | Where-Object { [string]$_.surface_variant -eq "settings-network-advanced" }).Count |
			Should Be 1
		@($settingsCases | Where-Object { [string]$_.surface_variant -eq "settings-audio-input-advanced" }).Count |
			Should Be 1
		@($settingsCases | Where-Object { [string]$_.surface_variant -eq "settings-audio-output-advanced" }).Count |
			Should Be 1
		@($settingsCases | Where-Object { [string]$_.surface_variant -eq "settings-messages-events" }).Count |
			Should Be 1
		@($settingsCases | Where-Object { [string]$_.surface_variant -eq "settings-messages-events-compact" }).Count |
			Should Be 1
		@($settingsCases | Where-Object { [string]$_.surface_variant -eq "settings-key-bindings-populated" }).Count |
			Should Be 1
		@($settingsCases | Where-Object { [string]$_.layout -eq "compact" }).Count | Should Be 5
		@($settingsCases | Where-Object { [double]$_.device_pixel_ratio -eq 1.5 }).Count | Should Be 1
		$eventMatrix = @($settingsCases | Where-Object {
			[string]$_.surface_variant -eq "settings-messages-events"
		})[0]
		[string]$eventMatrix.layout | Should Be "regular"
		[int]$eventMatrix.width | Should Be 1440
		$compactEventMatrix = @($settingsCases | Where-Object {
			[string]$_.surface_variant -eq "settings-messages-events-compact"
		})[0]
		[string]$compactEventMatrix.layout | Should Be "compact"
		[string]$compactEventMatrix.density | Should Be "compact"
		$populatedKeys = @($settingsCases | Where-Object {
			[string]$_.surface_variant -eq "settings-key-bindings-populated"
		})[0]
		[string]$populatedKeys.layout | Should Be "compact"
		[string]$populatedKeys.density | Should Be "compact"

		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$controller | Should Match '#include "ModernSettingsController.h"'
		$controller | Should Match 'ModernSettingsController controller;'
		$controller | Should Match 'controller\.open\(visualSettings, page\.value\(\)\);'
		$controller | Should Match 'controller\.state\(\)'
		foreach ($focusId in @("network.qos", "audio.vadMin", "audio.jitterBuffer")) {
			$controller | Should Match ([regex]::Escape('QStringLiteral("' + $focusId + '")'))
		}
		$controller | Should Match 'PersistentChatMediaCache::formattedSize\(0\)'
		$controller | Should Match 'QStringLiteral\("Windows"\)'
		$controller | Should Match 'visualSettings\.qlShortcuts = \{ pushToTalk, muteSelf \}'
		$controller | Should Match 'GlobalShortcutType::PushToTalk'
		$controller | Should Match 'messages\.toggleEvent'
		$controller | Should Match 'keys\.beginShortcutCapture'
		$controller | Should Match 'messageEventList'
		$controller | Should Match 'shortcutList'
		$controller | Should Not Match 'general\.language'
		$controller | Should Not Match 'look\.interfaceScale'
	}

	It "gates updating and partial plugin states through the production editor DTO" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$editor = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\PluginEditor.qml"
		$dialog = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\QmlDialog.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"

		foreach ($surface in @("settings-plugins-updating", "settings-plugins-partial")) {
			@($matrix.cases | Where-Object {
				[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -eq $surface
			}).Count | Should Be 1
			$controller | Should Match ([regex]::Escape('QStringLiteral("' + $surface + '")'))
		}
		$controller | Should Match 'ModernSettingsController controller;'
		$controller | Should Match 'variant\.startsWith\(QLatin1String\("settings-plugins"\)\)'
		$controller | Should Match 'QStringLiteral\("cancellable"\), true'
		$controller | Should Match 'QStringLiteral\("errorCode"\), QStringLiteral\("signature-invalid"\)'
		$controller | Should Match 'QStringLiteral\("initialFocusId"\), QStringLiteral\("pluginOperationCancelButton"\)'
		$editor | Should Match 'objectName:\s*"pluginOperationCancelButton"'
		$editor | Should Match 'function cancelCurrentOperation\(\)[\s\S]*asyncOperationController\.cancel'
		$editor | Should Match 'objectName:\s*"pluginOperationResult_"\s*\+\s*resultId'
		$editor | Should Match 'Accessible\.role:\s*Accessible\.ListItem[\s\S]*operationResultName'
		$dialog | Should Match 'PluginEditor\s*\{[\s\S]*asyncOperationController:[^\r\n]*operationModel'
		$worker | Should Match 'Updating plugins: Downloading Game telemetry'
		$worker | Should Match 'Failed: Game telemetry — Signature verification failed'
		$worker | Should Match '"settings-plugins"\s*\{\s*"Plugins"\s*\}'
		foreach ($name in @("Installed plugins", "Install plugin…", "Rescan", "Manual placement", "Configure", "About")) {
			$worker | Should Match ([regex]::Escape('"' + $name + '"'))
		}
		$worker | Should Match 'exposes a stale cancellation action'
	}

	It "keeps supported Windows inline-media fixtures explicit and network-independent" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$media = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -like "media-*"
		})
		$media.Count | Should Be 6
		foreach ($case in $media) {
			[string]$case.surface_variant | Should Match '^media-inline-'
			[string]$case.rich_preview_variant | Should Be "youtube"
			[string]$case.presentation_family | Should Be "embed"
			[string]$case.case_variant | Should Be "youtube"
			[bool]$case.presentation_only | Should Be $true
			($case.PSObject.Properties.Name -contains "url") | Should Be $false
		}
	}
}

Describe "Qt Quick visual runner modes" {
	It "keeps baseline gate and candidate capture mutually exclusive" {
		foreach ($scriptName in @("invoke-qml-visual-gate.ps1", "invoke-qml-visual-matrix.ps1")) {
			$command = Get-Command "$PSScriptRoot\..\$scriptName"
			$setNames = @($command.ParameterSets | ForEach-Object { $_.Name })
			($setNames -contains "Gate") | Should Be $true
			($setNames -contains "Candidate") | Should Be $true
			$gate = $command.ParameterSets | Where-Object Name -eq "Gate"
			$candidate = $command.ParameterSets | Where-Object Name -eq "Candidate"
			@($gate.Parameters | Where-Object Name -eq "BaselineManifestPath").Count | Should Be 1
			@($gate.Parameters | Where-Object Name -eq "CandidateOnly").Count | Should Be 0
			@($candidate.Parameters | Where-Object Name -eq "CandidateOnly").Count | Should Be 1
			@($candidate.Parameters | Where-Object Name -eq "BaselineManifestPath").Count | Should Be 0
		}
	}

	It "forces the deterministic software renderer only for matrix child processes" {
		$matrixRunner = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1"
		$matrixRunner.Contains("`$env:QT_QUICK_BACKEND = 'software'") | Should Be $true
		$matrixRunner.Contains("`$env:QSG_RHI_BACKEND = 'software'") | Should Be $true
		$matrixRunner.Contains("`$env:QSG_RENDER_LOOP = 'basic'") | Should Be $true
		($matrixRunner -match "renderer\s*=\s*'software'") | Should Be $true
	}

	It "restores every renderer environment variable in a finally block" {
		$matrixRunner = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1"
		$outerFinally = $matrixRunner.LastIndexOf('} finally {', [StringComparison]::Ordinal)
		($outerFinally -ge 0) | Should Be $true
		foreach ($name in @('QT_QUICK_BACKEND', 'QSG_RHI_BACKEND', 'QSG_RENDER_LOOP')) {
			$savedAssignment = $name + ' = $env:' + $name
			$restoreAssignment = '$env:' + $name + ' = $saved.' + $name
			$matrixRunner.Contains($savedAssignment) | Should Be $true
			($matrixRunner.IndexOf($restoreAssignment, $outerFinally, [StringComparison]::Ordinal) -gt $outerFinally) |
				Should Be $true
		}
	}

	It "records executable and source provenance in the published matrix manifest" {
		$matrixRunner = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1"
		($matrixRunner -match 'executable_sha256\s*=\s*Get-QmlVisualFileSha256\s+\$executablePath') |
			Should Be $true
		($matrixRunner -match 'source_git_sha\s*=\s*\$gitSha') | Should Be $true
	}

	It "forces each visual process offline without mutating the source config" {
		$matrixRunner = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1"
		$matrixRunner.Contains('New-IsolatedVisualConfig -SourcePath $sourceConfig -DestinationPath $configCopy') |
			Should Be $true
		$matrixRunner.Contains("Set-JsonObjectProperty -Object `$networkValue -Name 'auto_connect_to_last_server' -Value `$false") |
			Should Be $true
		$matrixRunner.Contains('[IO.File]::WriteAllText(') | Should Be $true
	}

	It "activates accessibility before the automation QML scene is constructed" {
		$main = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\main.cpp"
		$activation = $main.IndexOf('QAccessible::setActive(true)', [StringComparison]::Ordinal)
		$mainWindow = $main.IndexOf('new MainWindow(nullptr)', [StringComparison]::Ordinal)
		$activation | Should BeGreaterThan -1
		$mainWindow | Should BeGreaterThan $activation
	}
}

Describe "Qt Quick connected fixture contract" {
	It "keeps synthetic loading state free of transient async operations" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		($controller -match 'operations->clear\(\)') | Should Be $true
		($controller -match 'setConnectionState\(state\s*==\s*QLatin1String\("loading"\)\s*\?\s*QStringLiteral\("connecting"\)') |
			Should Be $true
		($controller -match 'startOperation\(QStringLiteral\("visual:loading"\)') | Should Be $false
	}

	It "publishes the Direct Messages header action without a live server connection" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		($controller -match 'DirectMessageController \*directMessages\s*=\s*m_host->directMessageController\(\)') |
			Should Be $true
		($controller -match 'directMessages->applyState\(\{[\s\S]*?QStringLiteral\("available"\), true') |
			Should Be $true
	}

	It "requires a runtime-observable timeline count" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'message_count') | Should Be $true
		($worker -match 'expectedMessageCount') | Should Be $true
	}

	It "negotiates, returns, and validates the requested MOTD variant" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($controller -match 'supported_motd_variants') | Should Be $true
		($controller -match 'QStringLiteral\("motd_variant"\),\s*motdVariant') | Should Be $true
		($worker -match 'supported_motd_variants') | Should Be $true
		($worker -match 'motd_variant\s*=\s*\$motdVariant') | Should Be $true
		($worker -match 'applied\.motd_variant\s*-ne\s*\$motdVariant') | Should Be $true
	}

	It "negotiates, returns, and accessibility-validates rich-preview variants" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($controller -match 'supported_rich_preview_variants') | Should Be $true
		($controller -match 'QStringLiteral\("rich_preview_variant"\),\s*richPreviewVariant') | Should Be $true
		($worker -match 'supported_rich_preview_variants') | Should Be $true
		($worker -match 'rich_preview_variant\s*=\s*\$richPreviewVariant') | Should Be $true
		($controller -match 'chatModel\(\)->get\(richPreviewRow\).*QStringLiteral\("preview"\)') | Should Be $true
		($worker -match 'qmlVisualGateRichPreviewState') | Should Be $true
		($worker -match 'RichPreviewTitle.*Grouping') | Should Be $true
		($worker -match 'rich_preview_embed_provider') | Should Be $true
		($worker -match 'RichPreviewPlayName') | Should Be $true
		($worker -match 'visibleImages') | Should Be $true
		($worker -match 'providerDetailsVisible') | Should Be $true
		($worker -match 'providerVariant') | Should Be $true
		($worker -match 'providerToken') | Should Be $true
		($worker -match 'providerFamily') | Should Be $true
		($worker -match 'providerPresentation') | Should Be $true
		($worker -match '"x"\s*\{[\s\S]*?variant = "x"; token = "x"; family = "social"; presentation = "socialPost"') |
			Should Be $true
		($worker -match '"x"\s*\{\s*"Social post"; break\s*\}') | Should Be $true
		($worker -match 'requiredRichPreviewStateProperties') | Should Be $true
		($worker -match 'missingRichPreviewStateProperties') | Should Be $true
		($worker -match '"settings-audio-input"\s*\{[\s\S]*?"Detection guide"[\s\S]*?"Set up voice activation"') |
			Should Be $true
		($worker -match 'cardX.*timelineX') | Should Be $true
		($worker -match 'steam.*providerSteamHeroImage') | Should Be $true
		($worker -match 'product.*vehicle.*property.*marketplace[\s\S]*providerCommerceHeroImage') |
			Should Be $true
		($worker -match 'article.*providerArticleHeroImage') | Should Be $true
		($worker -match 'previewState.*expectedPreviewState') | Should Be $true
		($worker -match 'compact.*expectedCompact') | Should Be $true
		($worker -match 'returned a truncated accessibility tree') | Should Be $true
	}

	It "negotiates the generic presentation family and case variant contract" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$models = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlClientModels.cpp"
		$automation = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		foreach ($token in @("supported_presentation_families", "supported_case_variants",
			"presentation_family", "case_variant")) {
			($controller -match [regex]::Escape($token)) | Should Be $true
			($worker -match [regex]::Escape($token)) | Should Be $true
		}
		($models -match 'QStringLiteral\("presentationFamily"\)') | Should Be $true
		($models -match 'QStringLiteral\("caseVariant"\)') | Should Be $true
		($worker -match 'linked-image semantic owner') | Should Be $true
		($controller -match 'QStringLiteral\("rich_body_message_id"\),\s*richBodyMessageId') | Should Be $true
		($automation -match 'qmlVisualGateRichBodyState') | Should Be $true
		($automation -match 'automationRichMessageBodyState') | Should Be $true
		($automation -match 'QStringLiteral\("modelImageCount"\)') | Should Be $true
		($automation -match 'QStringLiteral\("imageStatusName"\)') | Should Be $true
		($worker -match 'qmlVisualGateRichBodyState') | Should Be $true
		($worker -match 'did not finish its model parse and live image delegate') | Should Be $true
	}

	It "negotiates product surfaces, density, and window-local capture evidence" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$server = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		$shellHost = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlShellHost.cpp"
		foreach ($token in @("supported_surface_variants", "surface_variant", "surface_present",
			"capture_window", "supported_densities", "density")) {
			($controller -match [regex]::Escape($token)) | Should Be $true
			($worker -match [regex]::Escape($token)) | Should Be $true
		}
		($worker -match 'window\s*=\s*\$captureWindow') | Should Be $true
		($worker -match 'StartsWith\("settings-"[\s\S]*?\{ "settings" \}') | Should Be $true
		($controller -match 'surfaceVariant\.startsWith\(QLatin1String\("settings-"\)\)[\s\S]*?captureWindow\s*=\s*QStringLiteral\("settings"\)') | Should Be $true
		($shellHost -match 'normalizedWindowId == QLatin1String\("settings"\)[\s\S]*?surfaceId = QStringLiteral\("settings\.window"\)') | Should Be $true
		($worker -match '\$expectRichPreview\s+-and\s+-not\s+\$isMediaSurface') | Should Be $true
		($server -match 'qmlAccessibilitySnapshot[\s\S]*captureWindowTarget\(windowId') | Should Be $true
	}

	It "uses real typed QML controllers for surface fixtures" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$settingsController = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernSettingsController.cpp"
		foreach ($token in @("ScreenShareViewBackend", "AsyncOperationModel", "directMessageController()",
			"dialogController()->applyState", "showManualPluginTool", "showPttTool", "mediaSession()->openInline")) {
			($controller -match [regex]::Escape($token)) | Should Be $true
		}
		($controller -match 'ModernSettingsController controller;') | Should Be $true
		($controller -match 'controller\.open\(visualSettings, page\.value\(\)\);') | Should Be $true
		($settingsController -match 'QStringLiteral\("range"\)') | Should Be $true
		($settingsController -match 'QStringLiteral\("segmented"\)') | Should Be $true
	}

	It "uses production factories for DTO dialogs and a typed controller for recorder fixtures" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$mainWindow = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\MainWindow.cpp"
		$automation = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		$factory = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernProductDialogStateFactory.cpp"
		$qmlDialog = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\QmlDialog.qml"
		foreach ($call in @("certificateDialog", "screenShareEditorState", "screenShareEditorDialog")) {
			($controller -match ('ModernProductDialogs::' + $call)) | Should Be $true
			($mainWindow -match ('ModernProductDialogs::' + $call)) | Should Be $true
			($factory -match ([regex]::Escape($call) + '\(')) | Should Be $true
		}
		($automation -match 'ModernProductDialogs::certificateDialog') | Should Be $true
		foreach ($source in @($controller, $mainWindow, $automation, $factory)) {
			($source -match 'ModernProductDialogs::voiceRecorderDialog') | Should Be $false
		}
		($controller -match 'recorderController\(\)') | Should Be $true
		($controller -match 'applyVisualFixtureState') | Should Be $true
		($mainWindow -match 'ModernRecorderController') | Should Be $true
		($qmlDialog -match 'RecorderEditor') | Should Be $true
		foreach ($orphan in @("visualDialogBase", "dialog-plugins", "certificate.status", "recorder.path")) {
			($controller -match [regex]::Escape($orphan)) | Should Be $false
		}
		($factory -match 'QStringLiteral\("certificate-current"\)') | Should Be $true
		($factory -match 'QStringLiteral\("initialFocusId"\)') | Should Be $true
	}

	It "suppresses WebEngine creation for deterministic media surfaces" {
		$inline = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\InlineMediaPlayer.qml"
		$detached = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\MediaSessionWindow.qml"
		foreach ($component in @($inline, $detached)) {
			($component -match 'property string visualFixtureMode') | Should Be $true
			($component -match 'active:\s*[^\n]*normalizedVisualFixtureMode\.length === 0') | Should Be $true
			($component -match 'Deterministic media playback preview') | Should Be $true
		}
	}

	It "uses deterministic managed fixtures for every newly gated visual family" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		foreach ($variant in @("x", "github", "social", "vehicle", "property", "article",
			"sensitive", "direct-media", "weather", "place", "traffic", "marketplace",
			"link-digest", "vimeo", "dailymotion", "soundcloud")) {
			($controller -match ('variant\s*==\s*QLatin1String\("' + [regex]::Escape($variant) + '"\)')) |
				Should Be $true
		}
		($controller -match 'caseVariant\s*==\s*QLatin1String\("rich-image-link"\)') | Should Be $true
		($controller -match 'registerVisualPreviewImage\(m_host,\s*caseVariant') | Should Be $true
		($controller -match 'image://mumble|registerVisualPreviewImage') | Should Be $true
	}

	It "keeps every named provider fixture paired in light and dark themes" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$namedVariants = @(
			"youtube", "spotify", "tiktok", "vimeo", "dailymotion", "soundcloud", "instagram",
			"finance", "audio", "product", "steam", "google", "twitch", "flashback", "x",
			"github", "social", "vehicle", "property", "marketplace", "article", "weather",
			"place", "traffic", "link-digest", "sensitive", "direct-media"
		)
		foreach ($variant in $namedVariants) {
			$cases = @($matrix.cases | Where-Object {
				[string](Get-QmlVisualCaseValue $_ "rich_preview_variant" "") -eq $variant
			})
			$themes = @($cases | ForEach-Object { [string]$_.theme } | Sort-Object -Unique)
			($themes -contains "light") | Should Be $true
			($themes -contains "dark") | Should Be $true
		}
	}

	It "keeps managed inline-image fixtures on a visible, readiness-gated media path" {
		$automation = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		$component = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\RichPreviewCard.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"

		($automation -match 'automationRegisterPreviewImageValue') | Should Be $true
		($automation -match 'dataImageSourceCount\s*==\s*0') | Should Be $true
		($automation -match 'cardImageSources\s*>\s*0\s*&&\s*cardImageReady\s*==\s*0') | Should Be $true
		($component -match 'fallbackKind\s*===\s*"image"[\s\S]*safeRenderImageSource\(preview\.mediaUrl') |
			Should Be $true
		($worker -match 'visibleImageCount') | Should Be $true
		($worker -match 'imageReadyCount') | Should Be $true
		($worker -match 'imageLoadingCount\s*-eq\s*0') | Should Be $true
		($worker -match 'imageErrorCount\s*-eq\s*0') | Should Be $true
	}

	It "requires embed posters to own playback while ordinary cards retain their open surface" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match '\$openSurfaceReady') | Should Be $true
		($worker -match '-not \[bool\]\$richPreviewCardState\.openSurfaceVisible') | Should Be $true
	}

	It "limits loading previews to one safe origin link until hydration completes" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Loading rich-preview case.*exactly one safe origin link') | Should Be $true
		($worker -match 'Loading rich-preview case.*playback or overflow actions before hydration completes') | Should Be $true
		($worker -match '"Play video in chat", "Preview actions", "Watch together"') | Should Be $true
	}

	It "uses managed synthetic data for Steam, Google Search, Twitch, and Flashback" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$automation = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		foreach ($variant in @("steam", "google", "twitch", "flashback")) {
			($controller -match ('variant\s*==\s*QLatin1String\("' + $variant + '"\)')) | Should Be $true
		}
		($controller -match 'registerVisualPreviewImage\(host,\s*variant,\s*title,[\s\S]*Twitch live fixture') |
			Should Be $true
		($controller -match 'registerVisualPreviewImage\(host,\s*variant,\s*title,[\s\S]*Steam store fixture') |
			Should Be $true
		($controller -match 'QStringLiteral\("previewKind"\),\s*QStringLiteral\("gameStoreProduct"\)') |
			Should Be $true
		($controller -match 'QStringLiteral\("previewKind"\),\s*QStringLiteral\("googleSearch"\)') |
			Should Be $true
		($controller -match 'QStringLiteral\("previewKind"\),\s*QStringLiteral\("twitch"\)') |
			Should Be $true
		($controller -match 'QStringLiteral\("previewKind"\),\s*QStringLiteral\("forum"\)') |
			Should Be $true
		foreach ($property in @(
			"providerDetailsVisible", "providerVariant", "providerToken", "providerFamily", "providerPresentation"
		)) {
			($automation -match ('QStringLiteral\("' + $property + '"\)')) | Should Be $true
		}
	}

	It "validates the bespoke Twitch semantic owner by its provider-owned fallback" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match '"twitch"\s*\{\s*"Twitch stream"') | Should Be $true
		($worker -match '"twitch"\s*\{\s*"Mumble Dev"') | Should Be $false
		($worker -match 'StartsWith\(\$expectedProviderGroupName') | Should Be $true
		($worker -match '\$expectedProviderGroupName') | Should Be $true
	}

	It "keeps GitHub repository identity and detail semantics distinct" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$providerDetails = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ProviderDetails.qml"
		($worker -match '"github"\s*\{\s*"Repository details"') | Should Be $true
		($providerDetails -match 'providerGitHubRepository[\s\S]*Accessible\.name:\s*qsTr\("Repository details"\)') |
			Should Be $true
	}

	It "waits for exact rich-preview model and live delegate parity" {
		$automation = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		($automation -match 'chatModel\(\)->messages\(\)\s*==\s*expectedMessages') | Should Be $true
		($automation -match 'candidate->property\("renderActive"\)\.toBool\(\)') | Should Be $true
		($automation -match 'automationQuickItemHasVisibleAncestry\(candidate\)') | Should Be $true
	}

	It "rejects a rich-preview grouping whose semantic name does not match the title" {
		$workerPath = "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$tokens = $null
		$parseErrors = $null
		$ast = [Management.Automation.Language.Parser]::ParseFile(
			(Resolve-Path $workerPath).Path, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		foreach ($functionName in @(
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityEntries", "Get-QmlAccessibilityFocusSummary",
			"Test-QmlAccessibilityPathContains", "Test-QmlAccessibilityPositiveRect",
			"Assert-QmlAccessibilityModalSubtree", "Assert-QmlAccessibilityHeadingLayout",
			"Assert-QmlAccessibilityViewportBounds", "Assert-QmlAccessibilityEvidence"
		)) {
			$functionAst = $ast.Find({
				param($node)
				$node -is [Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq $functionName
			}, $true)
			$null -eq $functionAst | Should Be $false
			. ([scriptblock]::Create($functionAst.Extent.Text))
		}

		$snapshot = [pscustomobject]@{
			role = "Window"; name = "Fixture shell"; states = @("focused"); children = @(
				[pscustomobject]@{ role = "Grouping"; name = "Unrelated preview"; states = @(); children = @() },
				[pscustomobject]@{ role = "Link"; name = "Open on YouTube: Expected title"; states = @(); children = @() },
				[pscustomobject]@{ role = "Button"; name = "Play Expected title here"; states = @(); children = @() }
			)
		}
		$thrown = $null
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $snapshot -CaseId "semantic-mismatch" -State "empty" `
				-RichPreviewVariant "youtube" -RichPreviewTitle "Expected title" `
				-RichPreviewOpenLabel "Open on YouTube" -RichPreviewEmbedProvider "youtube"
		} catch { $thrown = $_ }
		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match "does not expose exactly one 'Expected title' grouping"
	}

	It "requires direct media playback and overflow actions without a competing closed-card link" {
		$workerPath = "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$worker = Get-Content -Raw $workerPath
		$worker | Should Match '\$inlineMediaStageExpected\s*=\s*\$richPreviewVariant\s+-eq\s*"direct-media"'
		$worker | Should Match '\$expectedImageObjectName\s*=\s*if\s*\(\$inlineMediaStageExpected\)\s*\{\s*"previewEmbedPoster"'
		$worker | Should Match '\$openSurfaceReady\s*=\s*if\s*\(\$inlineMediaStageExpected\)'
		$tokens = $null
		$parseErrors = $null
		$ast = [Management.Automation.Language.Parser]::ParseFile(
			(Resolve-Path $workerPath).Path, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		foreach ($functionName in @(
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityEntries", "Get-QmlAccessibilityFocusSummary",
			"Test-QmlAccessibilityPathContains", "Test-QmlAccessibilityPositiveRect",
			"Assert-QmlAccessibilityModalSubtree", "Assert-QmlAccessibilityHeadingLayout",
			"Assert-QmlAccessibilityViewportBounds", "Assert-QmlAccessibilityEvidence"
		)) {
			$functionAst = $ast.Find({
				param($node)
				$node -is [Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq $functionName
			}, $true)
			$null -eq $functionAst | Should Be $false
			. ([scriptblock]::Create($functionAst.Extent.Text))
		}

		$snapshot = [pscustomobject]@{
			role = "Window"; name = "Fixture shell"; states = @("focused"); children = @(
				[pscustomobject]@{ role = "Grouping"; name = "Direct clip: Direct media"; states = @(); children = @() },
				[pscustomobject]@{ role = "Button"; name = "Play video in chat"; states = @(); children = @() },
				[pscustomobject]@{ role = "Button"; name = "Preview actions"; states = @(); children = @() }
			)
		}
		$thrown = $null
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $snapshot -CaseId "direct-media-actions" -State "empty" `
				-RichPreviewVariant "direct-media" -RichPreviewTitle "Direct clip" `
				-RichPreviewOpenLabel "Open media source" -RichPreviewPlayName "Play video in chat"
		} catch { $thrown = $_ }
		$null -eq $thrown | Should Be $true

		$snapshot.children = @($snapshot.children | Where-Object { [string]$_.name -ne "Preview actions" })
		$thrown = $null
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $snapshot -CaseId "direct-media-actions" -State "empty" `
				-RichPreviewVariant "direct-media" -RichPreviewTitle "Direct clip" `
				-RichPreviewOpenLabel "Open media source" -RichPreviewPlayName "Play video in chat"
		} catch { $thrown = $_ }
		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match "Preview actions"
	}

	It "rejects a bespoke provider case without its semantic details grouping" {
		$workerPath = "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$tokens = $null
		$parseErrors = $null
		$ast = [Management.Automation.Language.Parser]::ParseFile(
			(Resolve-Path $workerPath).Path, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		foreach ($functionName in @(
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityEntries", "Get-QmlAccessibilityFocusSummary",
			"Test-QmlAccessibilityPathContains", "Test-QmlAccessibilityPositiveRect",
			"Assert-QmlAccessibilityModalSubtree", "Assert-QmlAccessibilityHeadingLayout",
			"Assert-QmlAccessibilityViewportBounds", "Assert-QmlAccessibilityEvidence"
		)) {
			$functionAst = $ast.Find({
				param($node)
				$node -is [Management.Automation.Language.FunctionDefinitionAst] -and
					$node.Name -eq $functionName
			}, $true)
			$null -eq $functionAst | Should Be $false
			. ([scriptblock]::Create($functionAst.Extent.Text))
		}

		$snapshot = [pscustomobject]@{
			role = "Window"; name = "Fixture shell"; states = @("focused"); children = @(
				[pscustomobject]@{ role = "Grouping"; name = "Hades II: Steam"; states = @(); children = @() },
				[pscustomobject]@{ role = "Link"; name = "Open on Steam: Hades II"; states = @(); children = @() }
			)
		}
		$thrown = $null
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $snapshot -CaseId "steam-details-missing" -State "empty" `
				-RichPreviewVariant "steam" -RichPreviewTitle "Hades II" `
				-RichPreviewOpenLabel "Open on Steam"
		} catch { $thrown = $_ }
		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match "Store details.*provider grouping"
	}

	It "keeps the server MOTD visible when the active conversation has user history" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($controller -match 'systemMessages\s*=\s*motdVariant\s*!=\s*QLatin1String\("none"\)') | Should Be $true
		($controller -match 'motdVariant\s*!=\s*QLatin1String\("history-visible"\)') | Should Be $true
		($controller -match 'QStringLiteral\("system"\),\s*systemMessages') | Should Be $true
		($worker -match 'motd_has_user_history') | Should Be $true
		($worker -match 'motd_visible') | Should Be $true
	}

	It "requires an accessible server MOTD affordance for every visible variant" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Server message of the day') | Should Be $true
		($worker -match 'Hide welcome message.*-notin\s+\$names') | Should Be $true
		($worker -match 'history-visible') | Should Be $true
	}

	It "resets transient MOTD persistence before every synthetic case" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		($controller -match 'setMotdContent\(\{\},\s*\{\}\)') | Should Be $true
		($controller -match 'setMotdExpanded\(true\)') | Should Be $true
		($controller -match 'setMotdDismissedSignature\(\{\}\)') | Should Be $true
		($controller -match 'setMotdLastSeenSignature\(\{\}\)') | Should Be $true
	}

	It "requires a non-empty deterministic focus target" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match '"focus_target"') | Should Be $true
		($worker -match 'IsNullOrWhiteSpace\(\[string\]\$applied\.focus_target\)') | Should Be $true
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		($mainQml -match 'return\s+"connectionBannerPrimaryAction"') | Should Be $true
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		($controller -match 'quickItemByObjectName\(presentationWindow->contentItem\(\), requestedFocusName\)') | Should Be $true
	}

	It "routes the participant menu fixture through the production navigation action path" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$controller | Should Match 'QStringLiteral\("isSelf"\), true'
		$mainQml | Should Match 'root\.navigationRailModel\.get\(index\)'
		$mainQml | Should Match 'rail\.requestParticipantMenu\(participantId'
		$mainQml | Should Not Match 'automation:participant:4294967295'
	}

	It "keeps the production app menu in window coordinates with a shadow-safe token inset" {
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$modernMenu = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ModernMenu.qml"
		$semanticMenu = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\SemanticMenu.qml"
		$mainQml | Should Match 'menu\.openAtLogicalPoint\(root\.contentItem,\s*point,\s*Theme\.space2'
		$modernMenu | Should Match 'parent\s*=\s*hostItem'
		$modernMenu | Should Match 'devicePixelRatio here is a\s*\n?\s*// correctness bug'
		$mainQml | Should Match '(?s)id:\s*appMenuPopup.*?parent:\s*root\.contentItem'
		$mainQml | Should Match 'alias === "appserver"[\s\S]*normalized\s*=\s*"app"'
		$mainQml | Should Match 'groupId === "server"[\s\S]*"label": ""'
		$semanticMenu | Should Match 'function representativeGroupIcon\(group, items\)'
		$semanticMenu | Should Match '"icon": representativeGroupIcon\(group, items\)'
		$semanticMenu | Should Not Match 'group\.icon\s*\|\|\s*"menu"'
	}

	It "deduplicates a generic disconnected status without dropping its accessibility detail" {
		$banner = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ConnectionBanner.qml"
		$banner | Should Match 'detailEchoesDisconnectedStatus'
		$banner | Should Match 'detailEchoesDisconnectedStatus\s*\?\s*""\s*:\s*suppliedDetail'
		$banner | Should Match 'suppliedDetail\.length\s*>\s*0\s*\?\s*suppliedDetail\s*:\s*detail'
		$banner | Should Match 'Accessible\.description:\s*accessibleDetail'
	}

	It "waits for delegates and focuses the running operation cancel action" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$operationCard = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\AsyncOperationCard.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($controller -match '(?s)presentationWindow->requestUpdate\(\);.*waitForPresentedFrame\(&frameError, presentationWindow\).*Initial visual surface presentation failed.*maximumFocusAttempts') | Should Be $true
		($controller -match '(?s)!requestedFocusItem \|\| requestedFocusName\.isEmpty\(\).*attempt \+ 1 < maximumFocusAttempts.*presentationWindow->requestUpdate\(\).*Visual focus target presentation failed.*continue;') | Should Be $true
		($controller -match '(?s)QElapsedTimer exposureElapsed.*QCoreApplication::processEvents.*std::atomic_bool.*QQuickWindow::frameSwapped.*QQuickWindow::afterFrameEnd.*QQuickWindow::afterRendering.*QQuickWindow::afterAnimating.*Qt::DirectConnection.*QElapsedTimer presentationElapsed.*frameEventTurns') | Should Be $true
		($controller -match 'guardedWindow->grabWindow\(\)') | Should Be $false
		($mainQml -match 'operationOverlay\.visualFixtureFocusTarget') | Should Be $true
		($operationCard -match '(?s)visualFixtureFocusTarget:.*cancelOperationButton\.visible.*cancelOperationButton') | Should Be $true
		($mainQml -match '(?s)surface\.indexOf\("async-"\) === 0.*operationCancelButton.*return operationTarget\.objectName.*return ""') |
			Should Be $true
		($worker -match 'focus_target\s*-ne\s*"operationCancelButton"') | Should Be $true
		($worker -match '(?s)Cancel Saving attachment.*-contains "focused"') | Should Be $true
		($controller -match '(?s)visual:attachment-save.*QString\(\).*Saving attachment.*Downloading original image') |
			Should Be $true
		($controller -match 'visual:update.*plugin-update') | Should Be $false
	}

	It "opens Manual Plugin before applying and verifies its deterministic fixture state" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$surfaceStart = $controller.IndexOf('bool QmlVisualFixtureController::applySurface')
		$manualStart = $controller.IndexOf('if (surfaceVariant == QLatin1String("manual-plugin"))', $surfaceStart)
		$showIndex = $controller.IndexOf('m_host->showManualPluginTool(true);', $manualStart)
		$setIndex = $controller.IndexOf('manual->setX(2.75)', $manualStart)
		($surfaceStart -ge 0) | Should Be $true
		($manualStart -gt $surfaceStart) | Should Be $true
		($showIndex -gt $manualStart) | Should Be $true
		($setIndex -gt $showIndex) | Should Be $true
		($controller -match 'manual_plugin_state') | Should Be $true
		($worker -match 'manual_plugin_state') | Should Be $true
		($worker -match 'visual-fixture:lobby') | Should Be $true
		($worker -match 'Demo User · Qt Quick') | Should Be $true
	}

	It "keeps Manual Plugin on its real tool surface without an orphan dialog probe" {
		$automation = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		$dialog = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\QmlDialog.qml"
		($automation -match 'openLifecycleDialogProbe') | Should Be $false
		($automation -match 'variant == QLatin1String\("manualPlugin"\)') | Should Be $false
		($automation -match 'manualPositionPreview') | Should Be $false
		($dialog -match 'manualPositionPreview') | Should Be $false
	}

	It "prevents responsive layout focus handoff from racing visual fixture focus" {
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		($mainQml -match '(?s)onCompactNavigationChanged:\s*\{.*visualFixtureOverrideActive') | Should Be $true
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		($controller -match '(?s)navigationCloseTimer.*navigationModalActive') | Should Be $true
	}

	It "requires focused accessibility nodes to form one nested ownership branch" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Get-QmlAccessibilityFocusSummary') | Should Be $true
		($worker -match 'StartsWith\("\$\(\$entry\.path\)/"') | Should Be $true
		($worker -match 'multiple independent focus branches') | Should Be $true
	}

	It "requires the accessibility tree to stabilize across queued scene turns" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'stableAccessibilitySamples') | Should Be $true
		($worker -match 'five identical observations') | Should Be $true
		($worker -match 'did not stabilize across five scene observations') | Should Be $true
	}

	It "requires three identical non-black frames across a stable time window before accepting a capture" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Get-QmlVisualPngCoverage') | Should Be $true
		($worker -match 'minimumNonBlackFraction') | Should Be $true
		($worker -match 'requiredStableFrameMatches\s*=\s*2') | Should Be $true
		($worker -match 'minimumStableFrameDurationMs\s*=\s*300') | Should Be $true
		($worker -match 'stableFrameSamples') | Should Be $true
		($worker -match 'stableFrameSince') | Should Be $true
		($worker -match 'three identical non-black frames stable') | Should Be $true
		($worker -match 'acceptedImageHash') | Should Be $true
		($worker -match 'finalImageHash') | Should Be $true
		($worker -match 'Compare-QmlVisualPng') | Should Be $true
		($worker -match 'finalPixelsExact') | Should Be $true
		($worker -match 'scene changed after accessibility stabilization') | Should Be $true
	}

	It "starts the strict stability budget after the first admissible frame" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$worker | Should Match 'initialCaptureDeadline\s*=\s*\[DateTime\]::UtcNow\.AddSeconds\(5\)'
		$worker | Should Match 'stabilityDeadline\s*=\s*\[DateTime\]::MinValue'
		$worker | Should Match '(?s)lastCoverage\s+-ge\s+\$minimumNonBlackFraction.*?stabilityDeadline\s+-eq\s+\[DateTime\]::MinValue.*?stabilityDeadline\s*=\s*\[DateTime\]::UtcNow\.AddSeconds\(5\).*?captureDeadline\s*=\s*\$stabilityDeadline'
	}

	It "waits out a stale pre-resize render target within the bounded capture deadline" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$worker | Should Match 'QQuickWindow::grabWindow can briefly expose the previous render target'
		$worker | Should Match '(?s)dimensions\.width\s+-ne\s+\$expectedWidth.*?Start-Sleep -Milliseconds 25.*?continue'
		$worker | Should Match 'did not present the requested.*render target'
	}

	It "keeps deferred accessibility reassertion bound to delegate lifetime" {
		$barrier = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ModalAccessibilityBarrier.qml"
		$messageFrame = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ChatMessageFrame.qml"
		$barrier | Should Match 'id:\s*reassertTimer'
		$barrier | Should Match 'id:\s*refreshTimer'
		$barrier | Should Not Match 'Qt\.callLater'
		$messageFrame | Should Match 'id:\s*accessibilityReassertTimer'
		$messageFrame | Should Not Match 'Qt\.callLater\(root\.reassertAccessibilitySuppression\)'
	}

	It "rejects stable black regions in every main-window capture" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Get-QmlVisualPngGridCoverage\s+-Path\s+\$imagePath\s+-Columns\s+3\s+-Rows\s+3') |
			Should Be $true
		($worker -match 'minimumMainGridNonBlackFraction\s*=\s*0\.10') | Should Be $true
		($worker -match 'mainGridComplete') | Should Be $true
		($worker -match 'finalMainGridComplete') | Should Be $true
		foreach ($field in @('accepted_main_grid_minimum', 'final_main_grid_minimum',
			'accepted_main_grid_cells', 'final_main_grid_cells')) {
			($worker -match [regex]::Escape($field)) | Should Be $true
		}
	}

	It "retains both frames and structured diagnostics when accessibility advances the scene" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$acceptedBytesIndex = $worker.IndexOf('$acceptedImageBytes = [IO.File]::ReadAllBytes($imagePath)')
		$finalHashIndex = $worker.IndexOf('$finalImageHash = Get-QmlVisualFileSha256 $imagePath', $acceptedBytesIndex)
		$decodedComparisonIndex = $worker.IndexOf('Compare-QmlVisualPng', $finalHashIndex)
		$diagnosticBranchIndex = $worker.IndexOf('-not $finalMainGridComplete -or -not $finalPixelsExact',
			$decodedComparisonIndex)
		$acceptedArtifactIndex = $worker.IndexOf('"$($case.id).accepted.png"', $diagnosticBranchIndex)
		$finalArtifactIndex = $worker.IndexOf('"$($case.id).after-accessibility.png"', $acceptedArtifactIndex)
		$frameChangeArtifactIndex = $worker.IndexOf('"$($case.id).frame-change.json"', $finalArtifactIndex)

		($acceptedBytesIndex -ge 0) | Should Be $true
		($finalHashIndex -gt $acceptedBytesIndex) | Should Be $true
		($decodedComparisonIndex -gt $finalHashIndex) | Should Be $true
		($diagnosticBranchIndex -gt $decodedComparisonIndex) | Should Be $true
		($acceptedArtifactIndex -gt $diagnosticBranchIndex) | Should Be $true
		($finalArtifactIndex -gt $acceptedArtifactIndex) | Should Be $true
		($frameChangeArtifactIndex -gt $finalArtifactIndex) | Should Be $true
		($worker -match '\[IO\.File\]::WriteAllBytes\(\$acceptedDiagnosticPath,\s*\$acceptedImageBytes\)') |
			Should Be $true
		($worker -match 'Copy-Item\s+-LiteralPath\s+\$imagePath\s+-Destination\s+\$finalDiagnosticPath\s+-Force') |
			Should Be $true
		foreach ($field in @('case_id', 'accepted_sha256', 'final_sha256', 'decoded_pixels_exact',
			'accepted_non_black_fraction', 'final_non_black_fraction')) {
			($worker -match [regex]::Escape($field)) | Should Be $true
		}
	}

	It "freezes async-operation layout animation only while the visual fixture override is active" {
		$mainQml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$operationCard = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\AsyncOperationCard.qml"
		($mainQml -match '(?s)Behavior on height\s*\{\s*enabled:\s*!root\.visualFixtureOverrideActive') | Should Be $true
		($operationCard -match '(?s)Behavior on height\s*\{.*enabled:\s*root\.animationsEnabled') | Should Be $true
	}

	It "rejects a leaf focus owner without a semantic name" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'IsNullOrWhiteSpace\(\[string\]\$focus\.leaf\.name\)') | Should Be $true
		($worker -match 'leaf focus owner has no semantic name') | Should Be $true
	}

	It "rejects invisible and offscreen accessibility nodes" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match '-contains "invisible"') | Should Be $true
		($worker -match '-contains "offscreen"') | Should Be $true
		($worker -match '\$hidden\.Count -ne 0') | Should Be $true
	}

	It "requires semantic drawer evidence for open compact cases" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'navigation_open') | Should Be $true
		($worker -match 'Rooms and participants') | Should Be $true
		($worker -match 'does not expose exactly one open navigation drawer') | Should Be $true
	}

	It "rejects duplicate semantic list rows after delegate reuse" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'duplicateListItems') | Should Be $true
		($worker -match 'duplicate semantic list items') | Should Be $true
		($worker -match 'ListItem[\s\S]*Group-Object') | Should Be $true
	}

	It "keeps compact drawer background subtrees out of accessibility" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Assert-QmlAccessibilityModalSubtree') | Should Be $true
		($worker -match 'outside its active dialog subtree') | Should Be $true
		($worker -match '\$expectedDialogName = if \(\$NavigationOpen\) \{ "Rooms and participants" \}') |
			Should Be $true
	}

	It "waits for the compact drawer position to reach its requested endpoint" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$server = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		($qml -match 'automationNavigationPosition:\s*navigationDrawer\.position') | Should Be $true
		($server -match 'QStringLiteral\("railPosition"\)') | Should Be $true
		($worker -match 'Test-NavigationEndpoint') | Should Be $true
		($worker -match 'Abs\(\[double\]\$Viewport\.railPosition\s*-\s*\$expectedRailPosition\)') | Should Be $true
	}

	It "maps the legacy server case to the real flat app menu without a sequential popup race" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$probe = [regex]::Match(
			$qml,
			'function openAutomationMenuProbe\(variant\)\s*\{(?<body>[\s\S]*?)\n\t\treturn \{'
		)
		$probe.Success | Should Be $true
		$body = $probe.Groups['body'].Value
		$preserveIndex = $body.IndexOf('const preserveOpenAppMenu')
		$resetIndex = $body.IndexOf('closeProductMenus()')
		$dispatchIndex = $body.IndexOf('if (normalized === "app")')
		($preserveIndex -ge 0) | Should Be $true
		($resetIndex -gt $preserveIndex) | Should Be $true
		($dispatchIndex -gt $resetIndex) | Should Be $true
		$body | Should Match 'const legacyAppServerAlias[\s\S]*normalized = "app"'
		$body | Should Match 'if \(!menu\.visible\)[\s\S]*openMenuAt\(menu'
		$body | Should Not Match 'normalized = "appServer"'
	}

	It "materializes and selects an actionable message before opening its product menu" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$qml | Should Match 'normalized === "message"[\s\S]*timeline\.forceLayout\(\)'
		$qml | Should Match 'candidate\.openAutomationActions && candidate\.hasMessageActions'
		$qml | Should Match 'message\.openAutomationActions\(\)'
	}

	It "requires one semantic PopupMenu for each product menu surface" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$worker | Should Match '\$surface\.StartsWith\("menu-"[\s\S]*role\)\.Equals\("PopupMenu"'
		$worker | Should Match '\$expectedMenuContainerCount = 1'
		$worker | Should Match 'menuContainers\.Count -ne \$expectedMenuContainerCount'
		$worker | Should Match '"menu-app" \{ @\([\s\S]*"Server information…"[\s\S]*"Access tokens…"'
		$worker | Should Match '"menu-app-server" \{ @\([\s\S]*"Server information…"[\s\S]*"Access tokens…"'
		$menu = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ModernMenu.qml"
		$menu | Should Match 'property:\s*"Accessible\.role"[\s\S]*Accessible\.PopupMenu'
	}

	It "reapplies menu edge constraints after QQuickMenu performs popup placement" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$menu = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ModernMenu.qml"
		$menuPlacement = [regex]::Match($qml,
			'function openMenuAt\(menu, anchorPoint(?:, focusReturnTarget)?\)\s*\{(?<body>[\s\S]*?)\n\t\}')
		$menuPlacement.Success | Should Be $true
		$body = $menuPlacement.Groups['body'].Value
		$body | Should Match 'menu\.openAtLogicalPoint'
		$menu | Should Match 'function effectivePopupHeight\(targetMenu\)[\s\S]*contentItem\.height'
		$menu | Should Match 'function openAtLogicalPoint\([\s\S]*openWithInitialFocus\(\)[\s\S]*place\(\)[\s\S]*Qt\.callLater'
	}

	It "keeps composer focus while suppressing only the visual-fixture cursor paint" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$qml | Should Match 'id:\s*composerTextCursorDelegate[\s\S]*visible:\s*!root\.visualFixtureOverrideActive'
		$qml | Should Match 'objectName:\s*"visualFixtureComposer"[\s\S]*cursorDelegate:\s*composerTextCursorDelegate'
	}

	It "suppresses the detached direct-message caret only in visual fixtures" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\DirectMessageWindow.qml"
		$qml | Should Match 'property bool visualFixtureMode:\s*false'
		$qml | Should Match 'id:\s*directMessageCursorDelegate[\s\S]*visible:\s*!root\.visualFixtureMode'
		$qml | Should Match 'objectName:\s*"directMessageComposer"[\s\S]*cursorDelegate:\s*directMessageCursorDelegate'
		$main = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$main | Should Match 'DirectMessageWindow\s*\{[\s\S]*visualFixtureMode:\s*root\.visualFixtureOverrideActive'
	}

	It "freezes detached loading animations only while visual fixtures are active" {
		$screenShare = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ScreenShareViewWindow.qml"
		$media = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\MediaSessionWindow.qml"
		$main = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"

		$screenShare | Should Match 'property bool visualFixtureMode:\s*false'
		$screenShare | Should Match 'objectName:\s*"screenShareBusyIndicator"[\s\S]*animated:\s*!root\.visualFixtureMode'
		$main | Should Match 'createScreenShareView\(backend\)[\s\S]*"visualFixtureMode":\s*root\.visualFixtureOverrideActive'
		$media | Should Match 'objectName:\s*"mediaSessionBusyIndicator"[\s\S]*animated:\s*mediaWindow\.normalizedVisualFixtureMode\.length\s*===\s*0'
		$media | Should Match 'Behavior on width\s*\{[\s\S]*enabled:\s*mediaWindow\.normalizedVisualFixtureMode\.length\s*===\s*0'
	}

	It "reasserts the generation-bound fixture focus before every image and accessibility capture" {
		$header = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.h"
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$server = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ModernUiAutomationServer.cpp"

		$header | Should Match 'bool ensureFocus\(const QString &windowId, QString \*error = nullptr\)'
		$controller | Should Match 'bool QmlVisualFixtureController::ensureFocus[\s\S]*requestActivate\(\)[\s\S]*forceActiveFocus\(Qt::TabFocusReason\)[\s\S]*Focus ownership is a GUI-item contract[\s\S]*QCoreApplication::processEvents'
		$controller | Should Match 'm_focusWindow = presentationWindow[\s\S]*m_focusItem = requestedFocusItem[\s\S]*m_focusWindowId = captureWindow[\s\S]*m_focusItemName = requestedFocusName'
		([regex]::Matches($server, 'ensureFocus\(windowId, &focusError\)')).Count | Should Be 2
	}

	It "requests a fresh scene-graph frame after installing every presentation completion hook" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$wait = [regex]::Match(
			$controller,
			'(?s)bool QmlVisualFixtureController::waitForPresentedFrame\(.*?\n\}')

		$wait.Success | Should Be $true
		$wait.Value | Should Match 'QQuickWindow::frameSwapped[\s\S]*QQuickWindow::afterFrameEnd[\s\S]*QQuickWindow::afterRendering[\s\S]*QQuickWindow::afterAnimating'
		$wait.Value | Should Match 'QQuickWindow::afterAnimating[\s\S]*guardedWindow->requestUpdate\(\)[\s\S]*while \(guardedWindow && !presented->load'
	}

	It "rebinds compact drawer focus to the stable navigationDrawerRooms list identity" {
		$main = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$rail = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\NavigationRail.qml"
		$header = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.h"
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"

		$main | Should Match 'roomListObjectName:\s*"navigationDrawerRooms"'
		$main | Should Match 'root\.compactNavigation\s*&&\s*navigationDrawer\.opened[\s\S]*navigationDrawerRail\.focusInitialItem\(\)[\s\S]*return navigationDrawerRail\.roomListObjectName'
		$rail | Should Match 'property string roomListObjectName:\s*"navigationRooms"'
		$rail | Should Match 'objectName:\s*navigationRail\.roomListObjectName'
		$header | Should Match 'QString m_focusState;'
		$header | Should Match 'QString m_focusSurfaceVariant;'
		$controller | Should Match 'ensureFocus[\s\S]*m_focusState[\s\S]*focusVisualFixture[\s\S]*m_focusSurfaceVariant'
		$worker | Should Match 'accessibility tree contains no focus owner or multiple independent focus branches'
		$worker | Should Match 'does not expose exactly one open navigation drawer to accessibility'
	}

	It "requires detached direct-message body text in accessibility evidence" {
		$window = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\DirectMessageWindow.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"

		$window | Should Match 'Array\.from\(value\)'
		$window | Should Match 'objectName:\s*"directMessageMessage_"\s*\+\s*stableId'
		$worker | Should Match 'Alex: The native DM surface feels fast\.'
		$worker | Should Match 'You: And private mode keeps this conversation local\.'
	}

	It "captures attachment and image viewers as focused accessible light and dark tool windows" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$shellHost = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlShellHost.cpp"
		$attachment = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\AttachmentViewer.qml"
		$image = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ImageViewer.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"

		foreach ($surface in @("attachment-viewer", "image-viewer")) {
			$cases = @($matrix.cases | Where-Object {
				[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -eq $surface
			})
			$cases.Count | Should Be 2
			@($cases | Where-Object { [string]$_.theme -eq "light" }).Count | Should Be 1
			@($cases | Where-Object { [string]$_.theme -eq "dark" }).Count | Should Be 1
			$controller | Should Match ([regex]::Escape('QStringLiteral("' + $surface + '")'))
			$shellHost | Should Match ([regex]::Escape('QLatin1String("' + $surface + '")'))
		}

		$attachment | Should Match 'surfaceId:\s*"attachmentViewer\.window"'
		$attachment | Should Match 'objectName:\s*"attachmentViewerImage"'
		$attachment | Should Match 'objectName:\s*"attachmentViewerImage"[\s\S]*Accessible\.role:\s*Accessible\.Graphic'
		$attachment | Should Match 'objectName:\s*"attachmentViewerSaveButton"[\s\S]*Accessible\.name:\s*qsTr\("Save original %1"\)'
		$image | Should Match 'surfaceId:\s*"imageViewer\.window"'
		$image | Should Match 'objectName:\s*"imageViewerImage"'
		$image | Should Match 'objectName:\s*"imageViewerImage"[\s\S]*Accessible\.role:\s*Accessible\.Graphic'
		$image | Should Match 'objectName:\s*"imageViewerFit"[\s\S]*Accessible\.name:\s*qsTr\("Fit image to window"\)'
		$controller | Should Match 'attachmentViewerImage[\s\S]*imageViewerImage[\s\S]*imageStatus\s*==\s*1'
		$shellHost | Should Match 'attachmentViewer\.window'
		$shellHost | Should Match 'imageViewer\.window'
		$worker | Should Match 'Save original Qt Quick attachment artwork'
		$worker | Should Match 'Image zoom controls'
		$worker | Should Match '\$surfaceVariant\s+-eq\s+"attachment-viewer"\)\s*\{\s*"attachment-viewer"\s*\}'
		$worker | Should Match '\$surfaceVariant\s+-eq\s+"image-viewer"\)\s*\{\s*"image-viewer"\s*\}'
		$worker | Should Match 'Viewer case.*single focused action'
		$worker | Should Match 'Viewer case.*does not expose exactly one.*graphic'
	}

	It "gates a real decoded screen-share frame behind fixture-only APIs" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$backendHeader = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ScreenShareViewBackend.h"
		$backendSource = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\ScreenShareViewBackend.cpp"
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$shellHost = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlShellHost.cpp"
		$viewer = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\ScreenShareViewWindow.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$testCMake = Get-Content -Raw "$PSScriptRoot\..\..\..\src\tests\TestScreenShare\CMakeLists.txt"

		$cases = @($matrix.cases | Where-Object {
			[string](Get-QmlVisualCaseValue $_ "surface_variant" "") -eq "screen-share-view-active"
		})
		$cases.Count | Should Be 2
		@($cases | Where-Object { [string]$_.theme -eq "light" }).Count | Should Be 1
		@($cases | Where-Object { [string]$_.theme -eq "dark" }).Count | Should Be 1
		@($cases | Where-Object { [string]$_.layout -eq "regular" }).Count | Should Be 1
		@($cases | Where-Object { [string]$_.layout -eq "compact" }).Count | Should Be 1
		$fixtureBuildGuard = '#if defined(MUMBLE_HAS_MODERN_UI_MOCKUPS) || defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)'
		$backendHeader | Should Match ('(?s)' + [regex]::Escape($fixtureBuildGuard) + '\s+void setVisualFixtureFrame\(const QImage &frame\);\s+#endif')
		$backendSource | Should Match ('(?s)' + [regex]::Escape($fixtureBuildGuard) + '\s+void ScreenShareViewBackend::setVisualFixtureFrame.*?#endif')
		@([regex]::Matches($controller, [regex]::Escape($fixtureBuildGuard))).Count | Should Be 4
		$controller | Should Match 'setVisualFixtureFrame\(visualScreenShareFrame\(\)\)'
		$controller | Should Match 'screen-share-view-loading"\),\s+QStringLiteral\("screenShareCloseButton"\)'
		$shellHost | Should Match '(?s)createScreenShareView.*setTransientParent\(m_window\).*registerCaptureWindow\(window\).*window->show\(\).*window->requestUpdate\(\)'
		$controller | Should Match '(?s)screenShareNativeVideoFrame.*isVisible\(\).*width\(\) > 0\.0.*height\(\) > 0\.0'
		$controller | Should Match 'QStringLiteral\("screen_share_native_frame_ready"\), screenShareNativeFrameReady'
		$viewer | Should Match 'objectName:\s*"screenShareNativeVideoFrame"[\s\S]*Accessible\.ignored:\s*!visible[\s\S]*Accessible\.role:\s*Accessible\.Graphic'
		$viewer | Should Match 'Accessible\.name:\s*qsTr\("Live shared screen frame"\)'
		$worker | Should Match 'screen_share_native_frame_ready'
		$worker | Should Match 'did not expose a ready native decoded-frame surface'
		$worker | Should Match 'did not focus the primary Pause control'
		$testCMake | Should Match 'target_compile_definitions\(TestScreenShare PRIVATE MUMBLE_HAS_MODERN_UI_MOCKUPS\)'
	}

	It "rejects product-background accessibility nodes behind every main-window modal" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"

		$worker | Should Match '\$modalSurface\s*=\s*\$surface\.StartsWith\("settings-"'
		$worker | Should Match '\$surface\.StartsWith\("dialog-"'
		$worker | Should Match '\$surface\.StartsWith\("screen-share-editor"'
		$worker | Should Match '\$modalAccessibilityActive\s*=\s*\$NavigationOpen -or \$modalSurface'
		$worker | Should Match '\$surfaceVariant\.StartsWith\("dialog-"[\s\S]*"product-dialog"'
		$worker | Should Match 'Assert-QmlAccessibilityModalSubtree -Snapshot \$Snapshot'
		$worker | Should Match 'outside its active dialog subtree'
	}

	It "requires plain fixture text and uses preview semantics for full-height rich content" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match [regex]::Escape('Welcome to the deterministic visual fixture.')) | Should Be $true
		($worker -match [regex]::Escape('Qt Quick is ready for review.')) | Should Be $true
		($worker -match [regex]::Escape('Conversation messages')) | Should Be $true
		($worker -match [regex]::Escape('$expectedMessages = @()')) | Should Be $true
		($worker -match [regex]::Escape('$RichPreviewVariant -eq "none" -and $CaseVariant -eq "none"')) | Should Be $true
		($worker -match '\$message -notin \$names') | Should Be $true
	}

	It "renders system message timestamps only in the semantic header" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($qml -match 'hasEmbeddedFooterContent:[\s\S]{0,180}\|\| \(own && !systemMessage && timestamp\.length > 0\)') | Should Be $true
		($qml -match '\(messageDelegate\.own\s*\r?\n\s*&& !messageDelegate\.systemMessage\)') | Should Be $true
		($worker -match 'outgoingTimestampNodes') | Should Be $true
		($worker -match 'expected exactly one semantic owner') | Should Be $true
	}

	It "rejects stale connected fixture messages in non-connected states" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match "Non-connected case.*exposes stale connected fixture message") | Should Be $true
		($worker -match '\$State -eq "connected"') | Should Be $true
		($worker -match 'Qt Quick is ready for review\.') | Should Be $true
	}

	It "seeds a valid versioned settings profile in the Windows visual workflow" {
		$workflow = Get-Content -Raw "$PSScriptRoot\..\..\..\.github\workflows\windows-shared-client.yml"
		($workflow -match '\{ "settings_version": 1 \}') | Should Be $true
		($workflow -match "'\{\}'\s*\|\s*Set-Content[^\r\n]*qml-visual-settings") | Should Be $false
	}
}

Describe "Qt Quick accessibility geometry and modal subtree contracts" {
	BeforeAll {
		$workerPath = "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$workerSource = Get-Content -Raw $workerPath
		$parseErrors = $null
		$workerAst = [Management.Automation.Language.Parser]::ParseInput(
			$workerSource, [ref]$null, [ref]$parseErrors)
		if (@($parseErrors).Count -ne 0) {
			throw "Visual-gate worker cannot be parsed for contract tests."
		}
		$functionNames = @(
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityEntries",
			"Get-QmlAccessibilityFocusSummary",
			"Test-QmlAccessibilityPathContains", "Test-QmlAccessibilityPositiveRect",
			"Assert-QmlAccessibilityModalSubtree", "Assert-QmlAccessibilityHeadingLayout",
			"Assert-QmlAccessibilityViewportBounds", "Assert-QmlAccessibilityEvidence"
		)
		$definitions = @($workerAst.FindAll({
			param($node)
			$node -is [Management.Automation.Language.FunctionDefinitionAst] -and
				$functionNames -contains $node.Name
		}, $true) | Where-Object { $functionNames -contains $_.Name })
		foreach ($name in $functionNames) {
			$definition = @($definitions | Where-Object Name -eq $name)
			if ($definition.Count -ne 1) {
				throw "Expected one '$name' definition in the visual-gate worker."
			}
			Invoke-Expression ([string]$definition[0].Extent.Text)
		}

		function New-QmlAccessibilityTestNode {
			param(
				[string]$Role,
				[string]$Name,
				[double]$X,
				[double]$Y,
				[double]$Width,
				[double]$Height,
				[object[]]$Children = @()
			)
			return [pscustomobject]@{
				role = $Role
				name = $Name
				description = ""
				states = @()
				rect = [pscustomobject]@{ x = $X; y = $Y; width = $Width; height = $Height }
				children = @($Children)
			}
		}
	}

	It "rejects leaked background siblings even when their names are unfamiliar" {
		$background = New-QmlAccessibilityTestNode "Button" "Future background action" 20 20 80 32
		$dialogAction = New-QmlAccessibilityTestNode "Button" "Done" 300 260 60 32
		$dialog = New-QmlAccessibilityTestNode "Dialog" "Settings" 100 80 320 240 @($dialogAction)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 480 320 @($background, $dialog)

		$threw = $false
		$message = ""
		try {
			Assert-QmlAccessibilityModalSubtree $root "settings-leak" "Settings"
		} catch {
			$threw = $true
			$message = [string]$_.Exception.Message
		}
		$threw | Should Be $true
		$message | Should Match "outside its active dialog subtree"
		$message | Should Match "Future background action"
	}

	It "accepts one isolated modal branch and nested confirmation dialogs" {
		$confirm = New-QmlAccessibilityTestNode "Dialog" "Confirm change" 160 140 180 100 @(
			(New-QmlAccessibilityTestNode "Button" "Confirm" 250 200 70 28)
		)
		$dialog = New-QmlAccessibilityTestNode "Dialog" "Settings" 80 40 360 260 @($confirm)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 480 320 @($dialog)

		$threw = $false
		try {
			Assert-QmlAccessibilityModalSubtree $root "settings-isolated" "Settings"
		} catch { $threw = $true }
		$threw | Should Be $false
	}

	It "rejects distinct visible headings whose rectangles overlap" {
		$textHeading = New-QmlAccessibilityTestNode "Heading" "TEXT ROOMS · 1" 10 148 339 34
		$voiceHeading = New-QmlAccessibilityTestNode "Heading" "VOICE ROOMS · 2" 10 148 339 34
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 760 700 @(
			$textHeading, $voiceHeading)

		$threw = $false
		$message = ""
		try {
			Assert-QmlAccessibilityHeadingLayout $root "overlapping-room-headings"
		} catch {
			$threw = $true
			$message = [string]$_.Exception.Message
		}
		$threw | Should Be $true
		$message | Should Match "overlapping headings"
		$message | Should Match "TEXT ROOMS"
		$message | Should Match "VOICE ROOMS"
	}

	It "accepts distinct headings once each section owns its own row" {
		$textHeading = New-QmlAccessibilityTestNode "Heading" "TEXT ROOMS · 1" 10 148 339 34
		$voiceHeading = New-QmlAccessibilityTestNode "Heading" "VOICE ROOMS · 2" 10 182 339 34
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 760 700 @(
			$textHeading, $voiceHeading)

		$threw = $false
		try { Assert-QmlAccessibilityHeadingLayout $root "separate-room-headings" }
		catch { $threw = $true }
		$threw | Should Be $false
	}

	It "rejects a compact timestamp outside the clipped product root" {
		$timestamp = New-QmlAccessibilityTestNode "StaticText" "10:25" 392 209 26 15
		$message = New-QmlAccessibilityTestNode "Client" "" 36 174 388 56 @($timestamp)
		$product = New-QmlAccessibilityTestNode "Client" "" 8 8 404 684 @($message)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 420 700 @($product)

		$threw = $false
		$errorText = ""
		try {
			Assert-QmlAccessibilityViewportBounds $root "compact-timestamp-clipped"
		} catch {
			$threw = $true
			$errorText = [string]$_.Exception.Message
		}
		$threw | Should Be $true
		$errorText | Should Match "StaticText '10:25'"
		$errorText | Should Match "outside ancestor"
	}

	It "accepts a compact timestamp inside every ancestor viewport" {
		$timestamp = New-QmlAccessibilityTestNode "StaticText" "10:25" 380 209 26 15
		$message = New-QmlAccessibilityTestNode "Client" "" 36 174 370 56 @($timestamp)
		$product = New-QmlAccessibilityTestNode "Client" "" 8 8 404 684 @($message)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 420 700 @($product)

		$threw = $false
		try { Assert-QmlAccessibilityViewportBounds $root "compact-timestamp-contained" }
		catch { $threw = $true }
		$threw | Should Be $false
	}

	It "accepts a semantic control extending two pixels beyond its layout wrapper" {
		$button = New-QmlAccessibilityTestNode "Button" "Attach images" 20 20 30 30
		$wrapper = New-QmlAccessibilityTestNode "Client" "" 20 20 100 28 @($button)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble" 0 0 160 80 @($wrapper)

		$threw = $false
		try { Assert-QmlAccessibilityViewportBounds $root "control-layout-overscan" }
		catch { $threw = $true }
		$threw | Should Be $false
	}

	It "accepts a full-height rich preview without offscreen fixture text" {
		$open = New-QmlAccessibilityTestNode "Link" `
			"Open on TikTok: Vertical creator preview" 850 720 260 32
		$play = New-QmlAccessibilityTestNode "Button" `
			"Play Vertical creator preview here" 930 330 96 48
		$preview = New-QmlAccessibilityTestNode "Grouping" `
			"Vertical creator preview: TikTok · @mumble" 830 120 380 660 @($play, $open)
		$timeline = New-QmlAccessibilityTestNode "List" "Conversation messages" `
			320 100 920 700 @($preview)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble Visual Fixture" `
			0 0 1280 900 @($timeline)
		$root.states = @("focused")

		$threw = $false
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $root -CaseId "preview-light-tiktok" `
				-State "connected" -RichPreviewVariant "tiktok" `
				-RichPreviewTitle "Vertical creator preview" `
				-RichPreviewOpenLabel "Open on TikTok" `
				-RichPreviewEmbedProvider "tiktok" `
				-RichPreviewPlayName "Play Vertical creator preview here" `
				-CaseVariant "tiktok"
		} catch { $threw = $true }
		$threw | Should Be $false
	}

	It "rejects a visible rich preview whose semantic preview subtree is absent" {
		$timeline = New-QmlAccessibilityTestNode "List" "Conversation messages" `
			320 100 920 700
		$root = New-QmlAccessibilityTestNode "Window" "Mumble Visual Fixture" `
			0 0 1280 900 @($timeline)
		$root.states = @("focused")

		$threw = $false
		$errorText = ""
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $root -CaseId "preview-light-tiktok" `
				-State "connected" -RichPreviewVariant "tiktok" `
				-RichPreviewTitle "Vertical creator preview" `
				-RichPreviewOpenLabel "Open on TikTok" `
				-RichPreviewEmbedProvider "tiktok" `
				-RichPreviewPlayName "Play Vertical creator preview here" `
				-CaseVariant "tiktok"
		} catch {
			$threw = $true
			$errorText = [string]$_.Exception.Message
		}
		$threw | Should Be $true
		$errorText | Should Match "does not expose exactly one 'Vertical creator preview' grouping"
	}

	It "rejects rich-preview evidence outside a semantic conversation timeline" {
		$preview = New-QmlAccessibilityTestNode "Grouping" `
			"Vertical creator preview: TikTok · @mumble" 830 120 380 660 @(
				(New-QmlAccessibilityTestNode "Button" "Play Vertical creator preview here" 930 330 96 48),
				(New-QmlAccessibilityTestNode "Link" "Open on TikTok: Vertical creator preview" 850 720 260 32)
			)
		$root = New-QmlAccessibilityTestNode "Window" "Mumble Visual Fixture" `
			0 0 1280 900 @($preview)
		$root.states = @("focused")

		$threw = $false
		$errorText = ""
		try {
			Assert-QmlAccessibilityEvidence -Snapshot $root -CaseId "preview-light-tiktok" `
				-State "connected" -RichPreviewVariant "tiktok" `
				-RichPreviewTitle "Vertical creator preview" `
				-RichPreviewOpenLabel "Open on TikTok" `
				-RichPreviewEmbedProvider "tiktok" `
				-RichPreviewPlayName "Play Vertical creator preview here" `
				-CaseVariant "tiktok"
		} catch {
			$threw = $true
			$errorText = [string]$_.Exception.Message
		}
		$threw | Should Be $true
		$errorText | Should Match "semantic conversation timeline"
	}
}

Describe "Qt Quick community parity contract" {
	BeforeAll {
		$script:communityRepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path
		$script:communityVisualWorker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$script:communityAutomation = Get-Content -Raw "$script:communityRepoRoot\src\mumble\ModernUiAutomationServer.cpp"
		$script:communityConnect = Get-Content -Raw "$script:communityRepoRoot\src\mumble\ModernConnectController.cpp"
		$script:communityConnectQml = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\QmlDialog.qml"
		$script:communityConnectTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestModernDialogControllers\TestModernDialogControllers.cpp"
		$script:communityMainWindow = Get-Content -Raw "$script:communityRepoRoot\src\mumble\MainWindow.cpp"
		$script:communityDialogTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_QmlDialog.qml"
		$script:communityRail = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\NavigationRail.qml"
		$script:communityRailTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_NavigationRail.qml"
		$script:communityDm = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\DirectMessageWindow.qml"
		$script:communityMain = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\Main.qml"
		$script:communityDmTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_DirectMessageSurfaces.qml"
		$script:communityManual = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\ManualPluginWindow.qml"
		$script:communityToolTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_ToolWindows.qml"
		$script:communityPreview = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\RichPreviewCard.qml"
		$script:communityProviderDetails = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\ProviderDetails.qml"
		$script:communityPreviewTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_RichChatComponents.qml"
		$script:communityToast = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\ToastPill.qml"
		$script:communityToastModel = Get-Content -Raw "$script:communityRepoRoot\src\mumble\QmlClientModels.cpp"
		$script:communityToastTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_ToastPill.qml"
	}

	It "drives product state through the typed in-process automation endpoint without Windows UIA" {
		foreach ($command in @(
			'qmlVisualGateCapabilities', 'setQmlVisualGateState', 'qmlVisualGateRichPreviewState',
			'captureQml', 'qmlAccessibilitySnapshot'
		)) {
			$script:communityVisualWorker | Should Match ([regex]::Escape('command = "' + $command + '"'))
			$script:communityAutomation | Should Match ([regex]::Escape('QLatin1String("' + $command + '")'))
		}
		$script:communityAutomation | Should Match 'if \(command == QLatin1String\("snapshot"\)\)'
		$script:communityAutomation | Should Match 'RoomModel \*rooms = host->roomModel\(\)'
		$script:communityAutomation | Should Match 'item\.insert\(QStringLiteral\("unreadCount"\), modelRow\.value\(QStringLiteral\("unreadCount"\)\)\)'
		foreach ($forbidden in @('UIAutomationClient', 'UIAutomationTypes', 'System\.Windows\.Automation',
			'AutomationElement', 'FindFirst\(', 'TreeWalker')) {
			($script:communityVisualWorker -match $forbidden) | Should Be $false
		}
	}

	It "publishes Favorites Public and LAN as one typed Connect source contract" {
		foreach ($field in @(
			'sources', 'activeSource', 'filter', 'sourceRows', 'selectedServerId',
			'selectedServerIndex', 'editorOpen', 'editorDirty', 'pendingConfirmation'
		)) {
			$script:communityConnect | Should Match ([regex]::Escape('QStringLiteral("' + $field + '")'))
		}
		$script:communityConnect | Should Match 'QStringList ids \{ QStringLiteral\("favorites"\), QStringLiteral\("public"\), QStringLiteral\("lan"\) \}'
		foreach ($field in @('id', 'label', 'status', 'count', 'filteredCount', 'error', 'canRetry', 'canCancel', 'selected')) {
			$script:communityConnect | Should Match ([regex]::Escape('item.insert(QStringLiteral("' + $field + '")'))
		}
		foreach ($status in @('idle', 'loading', 'ready', 'error', 'unavailable', 'cancelled')) {
			$script:communityConnect | Should Match ([regex]::Escape('"' + $status + '"'))
		}
		$script:communityConnectTest | Should Match 'connectControllerPublishesTypedDiscoverySourcesAndSafeEditorTransitions'
		$script:communityConnectTest | Should Match 'public:registry%3Astockholm-1'
		$script:communityConnectTest | Should Match 'studio-renamed\.local'
	}

	It "keeps Connect discovery keyboardable and makes retry cancel discard and remove explicit" {
		foreach ($objectName in @(
			'connectSourceBar', 'connectSourceSelector', 'connectServerFilter', 'connectSourceStatus',
			'connectSourceBusy', 'connectSourceCancelButton', 'connectSourceRetryButton', 'connectFavoriteList',
			'connectConfirmationPopup', 'connectConfirmationTitle', 'connectConfirmationMessage',
			'connectConfirmationCancel', 'connectConfirmationConfirm'
		)) {
			$script:communityConnectQml | Should Match ([regex]::Escape('objectName: "' + $objectName + '"'))
		}
		foreach ($action in @(
			'selectSource', 'retrySource', 'cancelSource', 'selectServer', 'connectServer',
			'dismissConfirmation', 'confirmDiscardEditor', 'confirmRemoveFavorite'
		)) {
			($script:communityConnectQml -match [regex]::Escape('"' + $action + '"') -or
				$script:communityConnect -match [regex]::Escape('"' + $action + '"')) | Should Be $true
		}
		$script:communityConnectQml | Should Match 'updateField\("connect\.filter", text\)'
		$script:communityDialogTest | Should Match 'test_connect_sources_expose_discovery_states_filter_keyboard_and_confirmations'
		$script:communityDialogTest | Should Match 'keyClick\(Qt\.Key_Down\)[\s\S]*keyClick\(Qt\.Key_Return\)'
		$script:communityDialogTest | Should Match 'lastAction, "dismissConfirmation"'
		$script:communityDialogTest | Should Match 'lastAction, "confirmRemoveFavorite"'
	}

	It "focuses a newly added server token exactly once without stealing later edits" {
		$script:communityMainWindow | Should Match 'openModernServerTokensDialog\(tokens, true, tokens\.size\(\) - 1\)'
		$script:communityMainWindow | Should Match 'QStringLiteral\("dialogField_token\.%1"\)\.arg\(initialFocusTokenIndex\)'
		$script:communityMainWindow | Should Match 'QStringLiteral\("add-token:%1"\)\.arg\(\+\+m_modernDialogFocusRequestSerial\)'
		$script:communityDialogTest | Should Match 'test_explicit_focus_request_retargets_same_surface_once'
		$script:communityDialogTest | Should Match 'focusRequestId": "add-token:1"'
		$script:communityDialogTest | Should Match 'focusRequestId": "add-token:2"'
	}

	It "keeps room filtering disclosure and talk-unread summaries on stable model rows" {
		foreach ($objectName in @(
			'navigationFilterBar', 'navigationFilterField', 'navigationFilterClear', 'navigationRooms',
			'navigationRoomDisclosure_', 'navigationRoomParticipantCount_',
			'navigationRoomParticipantCountLabel_', 'navigationRoomUnread_'
		)) {
			$script:communityRail | Should Match ([regex]::Escape('objectName: "' + $objectName))
		}
		$script:communityRail | Should Match 'talkingCountForRoom\(scopeToken, payload\)'
		$script:communityRail | Should Match 'qsTr\("%1 people speaking"\)'
		$script:communityRail | Should Match 'qsTr\("%1 unread messages"\)'
		$script:communityRailTest | Should Match 'test_voice_room_disclosure_preserves_collapsed_activity_summary'
		$script:communityRailTest | Should Match 'test_room_filter_is_keyboard_accessible_and_keeps_stable_rows'
		$script:communityRailTest | Should Match 'test_filter_and_collapse_never_hide_current_user_or_voice_selection'
		$script:communityRailTest | Should Match 'compare\(navigationList\.count, 6\)'
		$script:communityRailTest | Should Match 'compare\(countLabel\.text, "1/1"\)'
		$script:communityRailTest | Should Match 'indexOf\("1 person speaking"\)'
	}

	It "routes DM Watch Together and managed images through the native shell contract" {
		$script:communityDm | Should Match 'signal watchTogetherRequested\(string url, string provider, string title\)'
		$script:communityDm | Should Match 'signal managedImageOpenRequested\(string source, string title, string messageId\)'
		$script:communityDm | Should Match 'onWatchTogetherRequested:[\s\S]*root\.watchTogetherRequested\(url, provider, title\)'
		$script:communityDm | Should Match 'onImageOpenRequested:[\s\S]*root\.managedImageOpenRequested'
		$script:communityMain | Should Match 'onManagedImageOpenRequested:[\s\S]*root\.openManagedPreviewImage\(source, title, messageId\)'
		$script:communityMain | Should Match 'onWatchTogetherRequested:[\s\S]*root\.startWatchTogether\(url, provider, title\)'
		$script:communityMain | Should Match 'function openManagedPreviewImage\(source, title, messageId\)[\s\S]*root\.openAttachment'
		$script:communityMain | Should Match 'objectName:\s*"directMessageTrayDismissLayer"[\s\S]*onClicked:\s*directMessages\.setTrayOpen\(false\)'
		$script:communityDmTest | Should Match 'test_private_preview_routes_watch_together_and_preserves_size_preference'
		$script:communityDmTest | Should Match 'test_private_preview_routes_managed_image_to_native_viewer_contract'
		$script:communityDmTest | Should Match 'image://mumble/dm-managed-artwork\?g=1'
	}

	It "exposes live typed DM and Watch Together state for connected community gates" {
		foreach ($command in @(
			'directMessageState', 'sendDirectMessage', 'closeDirectMessage', 'setDirectMessageMode',
			'markDirectMessageRead', 'watchTogetherState', 'startWatchTogether', 'watchTogetherAction',
			'requestPreviewHydration', 'richPreviewState', 'invokeRichPreviewAction',
			'screenShareViewerState', 'screenShareViewerAction',
			'toastState', 'publishToast', 'dismissToast',
			'directMessageReply', 'directMessageCancelReply', 'directMessageRetry', 'directMessageDelete',
			'directMessageToggleReaction', 'directMessageChooseAttachment',
			'directMessageRemoveAttachment', 'directMessageRetryAttachment',
			'directMessageOpenAttachment', 'directMessageDownloadAttachment', 'directMessageHydrateContent'
		)) {
			$script:communityAutomation | Should Match ([regex]::Escape('QLatin1String("' + $command + '")'))
		}
		foreach ($field in @(
			'conversations', 'messages', 'windowCaptureReady',
			'sharedAvailable', 'sharedJoined', 'sharedHost', 'sharedSessionId', 'sharedScopeId',
			'sharedParticipantSessions', 'sharedOperationStatus', 'sharedOperationError', 'syncGeneration'
		)) {
			$script:communityAutomation | Should Match ([regex]::Escape('QStringLiteral("' + $field + '")'))
		}
		$script:communityAutomation | Should Match 'directMessages->sendDraft\(\)'
		$script:communityAutomation | Should Match 'media->startShared\(url, provider, title\)'
		$script:communityAutomation | Should Match 'media->joinShared\(\)'
		$script:communityAutomation | Should Match 'media->leaveShared\(\)'
		$script:communityAutomation | Should Match 'media->endShared\(\)'
		$script:communityAutomation | Should Match 'media->reopenSharedPlayer\(\)'
		$script:communityAutomation | Should Match 'requestPreviewHydration\(scopeToken, messageIds, highPriority\)'
		$script:communityAutomation | Should Match 'timeline->rowForStableId\(stableId\)'
		$script:communityAutomation | Should Match 'requestInlinePlaybackWithFocus'
		$script:communityAutomation | Should Match 'requestCurrentDirectMediaPopout'
		$script:communityAutomation | Should Match 'surfaceId.*screenShare\.viewer'
		$script:communityAutomation | Should Match 'backend->setPaused\(true\)'
		$script:communityAutomation | Should Match 'backend->setAudioMuted\(true\)'
		$script:communityAutomation | Should Match 'backend->setAudioVolume\(volume\)'
		$script:communityAutomation | Should Match 'backend->requestRetry\(\)'
		$script:communityAutomation | Should Match 'backend->requestStop\(\)'
		$script:communityAutomation | Should Match 'source\.insert\(QStringLiteral\("stableId"\), modelRow\.value\(QStringLiteral\("id"\)\)\)'
		$script:communityAutomation | Should Match 'directMessages->replyToMessage\(stableId\)'
		$script:communityAutomation | Should Match 'directMessages->toggleMessageReaction\(stableId, emoji\)'
		$script:communityAutomation | Should Match 'directMessages->requestContentHydration\(stableId,'
		$script:communityAutomation | Should Match 'requiresNativeDialog'
	}

	It "renders notifications as one centered coalescing product pill" {
		$script:communityMainWindow | Should Match 'toastController\(\)->publish\(kind, title, message, actionID, actionLabel, timeoutMs\)'
		$script:communityMain | Should Match 'ToastPill\s*\{[\s\S]*anchors\.horizontalCenter: parent\.horizontalCenter[\s\S]*anchors\.bottom: parent\.bottom[\s\S]*anchors\.bottomMargin: composerSurface\.height \+ bottomStonksTicker\.height \+ 22'
		$script:communityToast | Should Match 'objectName: "modernToastPill"'
		$script:communityToast | Should Match 'radius: height / 2'
		$script:communityToast | Should Match 'controller\.repeatCount > 1'
		$script:communityToast | Should Match '"\\u00d7" \+ String\(controller \? controller\.repeatCount : 1\)'
		$script:communityToastModel | Should Match 'const bool duplicate = m_visible'
		$script:communityToastModel | Should Match 'm_repeatCount = duplicate \? qMin\(m_repeatCount \+ 1, 999\) : 1'
		$script:communityToastTest | Should Match 'test_duplicate_counter_reuses_the_same_pill'
		$script:communityToastTest | Should Match 'compare\(badge\.text, "\\u00d74"\)'
		$script:communityAutomation | Should Match 'automationToastState\(host->toastController\(\)\)'
		$script:communityVisualWorker | Should Match '"toast-single"\s*\{[\s\S]*"Settings saved\. Your Modern client preferences are ready\."'
		$script:communityVisualWorker | Should Match '"manual_plugin_state", "recorder_state", "toast_state"'
		$script:communityVisualWorker | Should Match '\$applied\.focus_target -ne "toastDismissButton"'
		$script:communityVisualWorker | Should Match '\$toastState\.repeat_count -ne \$expectedRepeats'
		$script:communityMainWindow | Should Not Match 'QStringLiteral\("toast:%1"\)'
	}

	It "guards dirty Manual Plugin close paths without losing the draft" {
		foreach ($objectName in @(
			'manualPluginDirtyStatus', 'manualDiscardDialog',
			'manualKeepEditingButton', 'manualDiscardChangesButton'
		)) {
			$script:communityManual | Should Match ([regex]::Escape('objectName: "' + $objectName + '"'))
		}
		$script:communityManual | Should Match 'if \(!approvedClose && dirty\)'
		$script:communityToolTest | Should Match 'test_manualPluginInitialFocusAndDirtyCloseGuard'
		$script:communityToolTest | Should Match 'tool\.requestClose\(\)[\s\S]*discardDialog, "opened", true'
		$script:communityToolTest | Should Match 'tool\.close\(\)[\s\S]*discardDialog, "opened", true'
		$script:communityToolTest | Should Match 'compare\(manualPlugin\.context, baselineContext\)'
	}

	It "isolates every nested product modal from promoted background accessibility" {
		$mediaControls = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\MediaSessionControls.qml"
		$navigationRail = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\NavigationRail.qml"
		$chatMessageFrame = Get-Content -Raw "$script:communityRepoRoot\src\mumble\qml-shell\ChatMessageFrame.qml"
		$mediaControlsTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_MediaSessionControls.qml"
		$navigationRailTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_NavigationRail.qml"
		$chatTimelineTest = Get-Content -Raw "$script:communityRepoRoot\src\tests\TestQmlQuickComponents\qml\tst_ChatTimelineDelegate.qml"

		$script:communityMain | Should Match 'readonly property bool modalUiActive:[\s\S]{0,160}mediaSessionWindowUnavailable'
		$script:communityMain | Should Match 'id:\s*mediaWindowComponentFailurePopup[\s\S]*?modal:\s*true[\s\S]*?focus:\s*true'
		$script:communityMain | Should Match 'readonly property bool backgroundAccessibilitySuppressed:[\s\S]{0,160}mediaSessionWindowUnavailable'
		$script:communityMain | Should Match 'active:\s*root\.backgroundAccessibilitySuppressed'

		$script:communityConnectQml | Should Match 'id:\s*nestedModalAccessibilityBarrier'
		$script:communityConnectQml | Should Match 'targets:\s*\[\s*dialogHeader,\s*dialogStatusBanner,\s*dialogBodyLayout,\s*dialogFooter\s*\]'
		$script:communityConnectQml | Should Match 'Accessible\.ignored:\s*dialog\.nestedModalOpen'
		$script:communityDialogTest | Should Match 'dialogNestedModalAccessibilityBarrier'

		$script:communityManual | Should Match 'id:\s*discardAccessibilityBarrier'
		$script:communityManual | Should Match 'targets:\s*\[\s*scrollView,\s*manualPluginFooter\s*\]'
		$script:communityToolTest | Should Match 'manualDiscardAccessibilityBarrier'

		$mediaControls | Should Match 'id:\s*closePromptAccessibilityBarrier'
		$mediaControls | Should Match 'targets:\s*\[\s*controlsLayout\s*\]'
		$mediaControlsTest | Should Match 'test_close_confirmation_owns_accessibility_and_restores_the_opener'

		$navigationRail | Should Match 'property bool accessibilitySuppressed:\s*false'
		$navigationRail | Should Match 'objectName:\s*"navigationRailAccessibilityBarrier"[\s\S]*?active:\s*navigationRail\.accessibilitySuppressed[\s\S]*?targets:\s*\[\s*railContentLayout\s*\]'
		$navigationRailTest | Should Match 'test_modal_suppression_covers_virtualized_room_and_participant_rows'

		$chatMessageFrame | Should Match 'property bool accessibilitySuppressed:\s*false'
		$chatMessageFrame | Should Match 'objectName:\s*"chatMessageAccessibilityBarrier"[\s\S]*?active:\s*root\.accessibilitySuppressed[\s\S]*?targets:\s*\[\s*root\s*\]'
		$chatTimelineTest | Should Match 'test_modal_suppression_prunes_promoted_message_content_and_restores_it'
	}

	It "keeps Steam posters and HLS-DASH trailers on the lazy native media route" {
		$script:communityPreview | Should Match 'steamMediaItems: root\.mediaItems'
		$script:communityPreview | Should Match 'steamMediaIndex: root\.selectedMediaIndex'
		$script:communityProviderDetails | Should Match 'objectName: "previewSteamMediaRail"'
		$script:communityProviderDetails | Should Match 'objectName: "previewSteamMediaThumbnail_" \+ index'
		$script:communityProviderDetails | Should Match 'modelData\.thumbnail[\s\S]*modelData\.poster'
		$script:communityPreviewTest | Should Match 'test_steam_gallery_is_visible_and_selectable_without_expanding_the_card'
		$script:communityPreviewTest | Should Match 'test_steam_manifest_only_trailers_keep_managed_posters_and_lazy_media_contract'
		foreach ($token in @(
			'application/vnd.apple.mpegurl', 'application/dash\+xml',
			'steam-hls-poster', 'steam-dash-poster', 'master\.m3u8', 'manifest\.mpd'
		)) {
			$script:communityPreviewTest | Should Match $token
		}
		$script:communityPreviewTest | Should Match 'requestCurrentMedia\(\)[\s\S]*directMediaSpy\.count, 1'
		$script:communityPreviewTest | Should Match 'requestCurrentDirectMediaPopout\(\)[\s\S]*popoutDirectMediaSpy\.count, 1'
	}

	It "keeps new off-tail messages visible without covering the conversation center" {
		$script:communityMain | Should Match 'property int pendingTailMessageCount: 0'
		$script:communityMain | Should Match 'pendingTailMessageCount \+= timeline\.pendingTailInsertCount'
		$script:communityMain | Should Match 'objectName: "jumpToLatestButton"[\s\S]*anchors\.right: parent\.right'
		$script:communityMain | Should Match 'qsTr\("%n new message\(s\)", "", timeline\.pendingTailMessageCount\)'
		$script:communityMain | Should Match 'onStickToBottomChanged:[\s\S]*pendingTailMessageCount = 0'
	}

	It "retains representative visual candidates for every community-critical surface family" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		foreach ($caseId in @(
			'preview-dark-steam', 'preview-light-steam', 'surface-light-connect',
			'surface-dark-connect-editor', 'surface-compact-connect-validation',
			'surface-light-direct-message-main', 'surface-dark-direct-message-window',
			'surface-light-manual-plugin', 'surface-dark-toast-single',
			'surface-light-toast-duplicate'
		)) {
			@($matrix.cases | Where-Object { [string]$_.id -eq $caseId }).Count | Should Be 1
		}
	}
}
