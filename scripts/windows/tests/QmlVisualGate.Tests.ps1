Import-Module "$PSScriptRoot\..\QmlVisualGate.Common.psm1" -Force

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
		@($compact | Where-Object { [int]$_.width -lt 900 -and -not [bool]$_.navigation_open }).Count |
			Should BeGreaterThan 0
		@($compact | Where-Object { [int]$_.width -eq 420 -and [bool]$_.navigation_open }).Count |
			Should BeGreaterThan 0
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

	It "covers every deterministic MOTD surface and a constrained compact height" {
		$matrix = Get-Content -Raw "$PSScriptRoot\..\qml-visual-gate-matrix.json" | ConvertFrom-Json
		$motdCases = @($matrix.cases | Where-Object { $_.PSObject.Properties.Name -contains "motd_variant" })
		$motdCases.Count | Should Be 5
		@($motdCases | Where-Object { [string]$_.state -ne "connected" }).Count | Should Be 0
		foreach ($variant in @("expanded", "collapsed", "changed", "history-hidden")) {
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
			$_.PSObject.Properties.Name -contains "rich_preview_variant"
		})
		$previewCases.Count | Should Be 18
		@($previewCases | Where-Object { [string]$_.state -ne "connected" }).Count | Should Be 0
		foreach ($variant in @(
			"youtube", "spotify", "tiktok", "instagram", "finance", "audio", "product",
			"steam", "google", "twitch", "flashback", "loading", "error"
		)) {
			@($previewCases | Where-Object { [string]$_.rich_preview_variant -eq $variant }).Count |
				Should BeGreaterThan 0
		}
		foreach ($variant in @("steam", "google", "twitch", "flashback")) {
			@($previewCases | Where-Object { [string]$_.rich_preview_variant -eq $variant }).Count |
				Should Be 1
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
			[string]$_.rich_preview_size -eq "compact"
		}).Count | Should Be 1
		@($previewCases | Where-Object { [string]$_.rich_preview_size -eq "large" }).Count | Should Be 1
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
}

Describe "Qt Quick connected fixture contract" {
	It "keeps synthetic loading state free of transient async operations" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		($controller -match 'operations->clear\(\)') | Should Be $true
		($controller -match 'setConnectionState\(state\s*==\s*QLatin1String\("loading"\)\s*\?\s*QStringLiteral\("connecting"\)') |
			Should Be $true
		($controller -match 'startOperation\(QStringLiteral\("visual:loading"\)') | Should Be $false
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
		($worker -match 'cardX.*timelineX') | Should Be $true
		($worker -match 'previewState.*expectedPreviewState') | Should Be $true
		($worker -match 'compact.*expectedCompact') | Should Be $true
		($worker -match 'returned a truncated accessibility tree') | Should Be $true
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

	It "keeps loading previews non-actionable until hydration completes" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Loading rich-preview case.*open link before hydration completes') | Should Be $true
		($worker -match '\$RichPreviewVariant -ne "loading"') | Should Be $true
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
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityFocusSummary", "Assert-QmlAccessibilityEvidence"
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

	It "rejects a bespoke provider case without its semantic details grouping" {
		$workerPath = "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		$tokens = $null
		$parseErrors = $null
		$ast = [Management.Automation.Language.Parser]::ParseFile(
			(Resolve-Path $workerPath).Path, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		foreach ($functionName in @(
			"Get-QmlAccessibilityNodes", "Get-QmlAccessibilityFocusSummary", "Assert-QmlAccessibilityEvidence"
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
		[string]$thrown.Exception.Message | Should Match "Store details.*provider-details grouping"
	}

	It "uses system timeline rows for visible MOTDs and user history for the hidden variant" {
		$controller = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\QmlVisualFixtureController.cpp"
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($controller -match 'systemMessages\s*=\s*motdVariant\s*!=\s*QLatin1String\("none"\)') | Should Be $true
		($controller -match 'motdVariant\s*!=\s*QLatin1String\("history-hidden"\)') | Should Be $true
		($controller -match 'QStringLiteral\("system"\),\s*systemMessages') | Should Be $true
		($worker -match 'motd_has_user_history') | Should Be $true
		($worker -match 'motd_visible') | Should Be $true
	}

	It "requires accessible visible and history-hidden MOTD affordances" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Server message of the day') | Should Be $true
		($worker -match 'Hide welcome message.*-notin\s+\$names') | Should Be $true
		($worker -match 'history-hidden.*Show welcome message.*-notin\s+\$names') | Should Be $true
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
		($controller -match 'quickItemByObjectName\(window->contentItem\(\), requestedFocusName\)') | Should Be $true
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

	It "requires two identical non-black frames before accepting a capture" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'Get-QmlVisualPngCoverage') | Should Be $true
		($worker -match 'minimumNonBlackFraction') | Should Be $true
		($worker -match 'stableFrameSamples') | Should Be $true
		($worker -match 'two identical non-black frames') | Should Be $true
		($worker -match 'acceptedImageHash') | Should Be $true
		($worker -match 'finalImageHash') | Should Be $true
		($worker -match 'scene changed after accessibility stabilization') | Should Be $true
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

	It "keeps compact drawer background controls out of accessibility" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'backgroundNames') | Should Be $true
		($worker -match 'Message Lobby') | Should Be $true
		($worker -match 'exposes background product control') | Should Be $true
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

	It "closes stale product menus before resolving a live-context menu probe" {
		$qml = Get-Content -Raw "$PSScriptRoot\..\..\..\src\mumble\qml-shell\Main.qml"
		$probe = [regex]::Match(
			$qml,
			'function openAutomationMenuProbe\(variant\)\s*\{(?<body>[\s\S]*?)\n\t\treturn \{'
		)
		$probe.Success | Should Be $true
		$body = $probe.Groups['body'].Value
		$resetIndex = $body.IndexOf('closeProductMenus()')
		$dispatchIndex = $body.IndexOf('if (normalized === "app")')
		($resetIndex -ge 0) | Should Be $true
		($dispatchIndex -gt $resetIndex) | Should Be $true
	}

	It "requires both connected fixture messages in accessibility evidence" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match [regex]::Escape('Welcome to the deterministic visual fixture.')) | Should Be $true
		($worker -match [regex]::Escape('Qt Quick is ready for review.')) | Should Be $true
		($worker -match '\$message -notin \$names') | Should Be $true
	}

	It "rejects stale connected fixture messages in non-connected states" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match "Non-connected case.*exposes stale connected fixture message") | Should Be $true
		($worker -match '\$State -eq "connected"') | Should Be $true
		($worker -match 'Qt Quick is ready for review\.') | Should Be $true
	}
}
