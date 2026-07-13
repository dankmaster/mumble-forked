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
}

Describe "Qt Quick connected fixture contract" {
	It "requires a runtime-observable timeline count" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'message_count') | Should Be $true
		($worker -match 'expectedMessageCount') | Should Be $true
	}

	It "requires a non-empty deterministic focus target" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match '"focus_target"') | Should Be $true
		($worker -match 'IsNullOrWhiteSpace\(\[string\]\$applied\.focus_target\)') | Should Be $true
	}

	It "requires exactly one focused accessibility node" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match '@\(\$_\.states\) -contains "focused"') | Should Be $true
		($worker -match '\$focused\.Count -ne 1') | Should Be $true
	}

	It "requires the accessibility tree to stabilize across queued scene turns" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'stableAccessibilitySamples') | Should Be $true
		($worker -match 'five identical observations') | Should Be $true
		($worker -match 'did not stabilize across five scene observations') | Should Be $true
	}

	It "rejects a focused accessibility node without a semantic name" {
		$worker = Get-Content -Raw "$PSScriptRoot\..\invoke-qml-visual-gate.ps1"
		($worker -match 'IsNullOrWhiteSpace\(\[string\]\$focused\[0\]\.name\)') | Should Be $true
		($worker -match 'focused accessibility node has no semantic name') | Should Be $true
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
