Import-Module "$PSScriptRoot\..\QmlVisualGate.Common.psm1" -Force

Describe "Qt Quick visual baseline integrity" {
	BeforeAll {
		$script:testRoot = Join-Path ([IO.Path]::GetTempPath()) (
			"qml-visual-baseline-integrity-" + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $script:testRoot | Out-Null
		$script:updateScript = (Resolve-Path "$PSScriptRoot\..\update-qml-visual-baseline.ps1").Path
		$script:gateScript = (Resolve-Path "$PSScriptRoot\..\invoke-qml-visual-gate.ps1").Path
		$script:matrixScript = (Resolve-Path "$PSScriptRoot\..\invoke-qml-visual-matrix.ps1").Path

		function New-IntegrityCaseRoot {
			param([Parameter(Mandatory = $true)][string]$Name)
			$path = Join-Path $script:testRoot $Name
			New-Item -ItemType Directory -Path $path | Out-Null
			return $path
		}

		function New-IntegrityMatrix {
			param(
				[Parameter(Mandatory = $true)][string]$Path,
				[Parameter(Mandatory = $true)][string[]]$CaseIds
			)
			$matrix = [ordered]@{
				schema_version = 1
				cases = @($CaseIds | ForEach-Object {
					[ordered]@{
						id = $_; width = 640; height = 520; device_pixel_ratio = 1.0
						theme = 'dark'; layout = 'regular'; state = 'connected'
					}
				})
			}
			$matrix | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
			return Get-QmlVisualFileSha256 $Path
		}

		function New-IntegrityCandidate {
			param(
				[Parameter(Mandatory = $true)][string]$Directory,
				[Parameter(Mandatory = $true)][string[]]$CaseIds,
				[Parameter(Mandatory = $true)][string]$MatrixHash,
				[string]$ProcessIsolation = 'per-dpr'
			)
			New-Item -ItemType Directory -Force -Path $Directory | Out-Null
			$cases = @($CaseIds | ForEach-Object {
				$id = $_
				$imagePath = Join-Path $Directory "$id.png"
				$accessibilityPath = Join-Path $Directory "$id.accessibility.json"
				$bitmap = [Drawing.Bitmap]::new(3, 2, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
				try {
					$bitmap.SetPixel(0, 0, [Drawing.Color]::FromArgb(255, 42, 84, 126))
					$bitmap.Save($imagePath, [Drawing.Imaging.ImageFormat]::Png)
				} finally { $bitmap.Dispose() }
				[ordered]@{ role = 'Window'; name = $id; states = @('focused'); children = @() } |
					ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $accessibilityPath -Encoding utf8NoBOM
				[ordered]@{
					id = $id
					image_sha256 = Get-QmlVisualFileSha256 $imagePath
					accessibility_sha256 = Get-QmlVisualFileSha256 $accessibilityPath
					image_width = 3
					image_height = 2
				}
			})
			$manifest = [ordered]@{
				schema_version = 1
				frontend = 'qml'
				process_isolation = $ProcessIsolation
				renderer = 'software'
				mode = 'candidate-only'
				matrix_sha256 = $MatrixHash
				executable_sha256 = ('a' * 64)
				source_git_sha = ('b' * 40)
				cases = $cases
			}
			$manifestPath = Join-Path $Directory 'manifest.json'
			$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
			return $manifestPath
		}
	}

	AfterAll {
		$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
		$resolvedTestRoot = [IO.Path]::GetFullPath($script:testRoot)
		if ($resolvedTestRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
			Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
		}
	}

	It "accepts only the exact case set and current matrix hash" {
		$root = New-IntegrityCaseRoot 'exact-contract'
		$matrixPath = Join-Path $root 'matrix.json'
		$matrixHash = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha', 'beta')
		$candidate = Join-Path $root 'candidate'
		$manifestPath = New-IntegrityCandidate -Directory $candidate -CaseIds @('beta', 'alpha') `
			-MatrixHash $matrixHash
		$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json

		Assert-QmlVisualManifestMatchesMatrix -Manifest $manifest -MatrixPath $matrixPath `
			-RequireCombinedCandidate | Should Be $true
	}

	It "accepts the in-memory ordered manifest produced by the matrix runner" {
		$root = New-IntegrityCaseRoot 'ordered-runner-manifest'
		$matrixPath = Join-Path $root 'matrix.json'
		$matrixHash = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha')
		$candidate = Join-Path $root 'candidate'
		$manifestPath = New-IntegrityCandidate -Directory $candidate -CaseIds @('alpha') `
			-MatrixHash $matrixHash
		$parsed = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
		$orderedManifest = [ordered]@{
			schema_version = $parsed.schema_version
			frontend = $parsed.frontend
			process_isolation = $parsed.process_isolation
			renderer = $parsed.renderer
			mode = $parsed.mode
			matrix_sha256 = $parsed.matrix_sha256
			cases = @($parsed.cases)
		}

		Assert-QmlVisualManifestMatchesMatrix -Manifest $orderedManifest -MatrixPath $matrixPath `
			-RequireCombinedCandidate | Should Be $true
	}

	It "rejects a manifest bound to a stale matrix hash" {
		$root = New-IntegrityCaseRoot 'stale-hash'
		$matrixPath = Join-Path $root 'matrix.json'
		$null = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha', 'beta')
		$candidate = Join-Path $root 'candidate'
		$manifestPath = New-IntegrityCandidate -Directory $candidate -CaseIds @('alpha', 'beta') `
			-MatrixHash ('0' * 64)
		$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
		$thrown = $null
		try { Assert-QmlVisualManifestMatchesMatrix -Manifest $manifest -MatrixPath $matrixPath | Out-Null }
		catch { $thrown = $_ }

		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match 'matrix hash does not match'
	}

	It "rejects a per-DPR partial candidate before touching the baseline" {
		$root = New-IntegrityCaseRoot 'partial-candidate'
		$matrixPath = Join-Path $root 'matrix.json'
		$matrixHash = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha', 'beta')
		$candidate = Join-Path $root 'candidate'
		$null = New-IntegrityCandidate -Directory $candidate -CaseIds @('alpha') -MatrixHash $matrixHash `
			-ProcessIsolation 'single-dpr'
		$baseline = Join-Path $root 'baseline'
		New-Item -ItemType Directory -Path $baseline | Out-Null
		'sentinel' | Set-Content -LiteralPath (Join-Path $baseline 'keep.txt') -Encoding utf8NoBOM
		$thrown = $null
		try {
			& $script:updateScript -CandidateDirectory $candidate -BaselineDirectory $baseline `
				-MatrixPath $matrixPath -AcceptReviewedCandidates -Confirm:$false
		} catch { $thrown = $_ }

		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match 'case set does not match'
		(Get-Content -Raw -LiteralPath (Join-Path $baseline 'keep.txt')).Trim() | Should Be 'sentinel'
		Test-Path -LiteralPath (Join-Path $baseline 'manifest.json') | Should Be $false
	}

	It "rejects a full worker manifest that did not come from the combined per-DPR runner" {
		$root = New-IntegrityCaseRoot 'worker-manifest'
		$matrixPath = Join-Path $root 'matrix.json'
		$matrixHash = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha', 'beta')
		$candidate = Join-Path $root 'candidate'
		$null = New-IntegrityCandidate -Directory $candidate -CaseIds @('alpha', 'beta') `
			-MatrixHash $matrixHash -ProcessIsolation 'single-dpr'
		$baseline = Join-Path $root 'baseline'
		$thrown = $null
		try {
			& $script:updateScript -CandidateDirectory $candidate -BaselineDirectory $baseline `
				-MatrixPath $matrixPath -AcceptReviewedCandidates -Confirm:$false
		} catch { $thrown = $_ }

		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match 'complete candidate-only manifest produced by the per-DPR matrix runner'
		Test-Path -LiteralPath $baseline | Should Be $false
	}

	It "replaces the complete baseline and removes only stale visual artifacts" {
		$root = New-IntegrityCaseRoot 'replace-baseline'
		$matrixPath = Join-Path $root 'matrix.json'
		$matrixHash = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha', 'beta')
		$candidate = Join-Path $root 'candidate'
		$null = New-IntegrityCandidate -Directory $candidate -CaseIds @('alpha', 'beta') -MatrixHash $matrixHash
		$baseline = Join-Path $root 'baseline'
		New-Item -ItemType Directory -Path $baseline | Out-Null
		'old image' | Set-Content -LiteralPath (Join-Path $baseline 'retired.png') -Encoding utf8NoBOM
		'old accessibility' | Set-Content -LiteralPath (Join-Path $baseline 'retired.accessibility.json') -Encoding utf8NoBOM
		'preserve me' | Set-Content -LiteralPath (Join-Path $baseline 'README.txt') -Encoding utf8NoBOM

		& $script:updateScript -CandidateDirectory $candidate -BaselineDirectory $baseline `
			-MatrixPath $matrixPath -AcceptReviewedCandidates -Confirm:$false

		Test-Path -LiteralPath (Join-Path $baseline 'retired.png') | Should Be $false
		Test-Path -LiteralPath (Join-Path $baseline 'retired.accessibility.json') | Should Be $false
		Test-Path -LiteralPath (Join-Path $baseline 'README.txt') | Should Be $true
		$actualArtifacts = @(Get-ChildItem -LiteralPath $baseline -File | Where-Object {
			$_.Name -like '*.png' -or $_.Name -like '*.accessibility.json'
		} | ForEach-Object Name | Sort-Object)
		$expectedArtifacts = @(
			'alpha.accessibility.json', 'alpha.png', 'beta.accessibility.json', 'beta.png'
		) | Sort-Object
		($actualArtifacts -join '|') | Should Be ($expectedArtifacts -join '|')
		$copiedManifest = Get-Content -Raw -LiteralPath (Join-Path $baseline 'manifest.json') | ConvertFrom-Json
		Assert-QmlVisualManifestMatchesMatrix -Manifest $copiedManifest -MatrixPath $matrixPath `
			-RequireCombinedCandidate | Should Be $true
		[string]$copiedManifest.executable_sha256 | Should Be ('a' * 64)
		[string]$copiedManifest.source_git_sha | Should Be ('b' * 40)
	}

	It "invalidates stale published output before a failed matrix rerun" {
		$root = New-IntegrityCaseRoot 'failed-rerun'
		$matrixPath = Join-Path $root 'matrix.json'
		$null = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha')
		$staleBaseline = Join-Path $root 'stale-baseline'
		$manifestPath = New-IntegrityCandidate -Directory $staleBaseline -CaseIds @('alpha') `
			-MatrixHash ('f' * 64)
		$configPath = Join-Path $root 'settings.json'
		$fakeExecutable = Join-Path $root 'not-an-executable.exe'
		'{}' | Set-Content -LiteralPath $configPath -Encoding utf8NoBOM
		'not executable' | Set-Content -LiteralPath $fakeExecutable -Encoding utf8NoBOM
		$output = Join-Path $root 'published-output'
		New-Item -ItemType Directory -Path $output | Out-Null
		'{ "stale": true }' | Set-Content -LiteralPath (Join-Path $output 'manifest.json') -Encoding utf8NoBOM
		'old image' | Set-Content -LiteralPath (Join-Path $output 'alpha.png') -Encoding utf8NoBOM

		$thrown = $null
		try {
			& $script:matrixScript -Executable $fakeExecutable -ConfigPath $configPath `
				-MatrixPath $matrixPath -BaselineManifestPath $manifestPath -OutputDirectory $output
		} catch { $thrown = $_ }

		$null -eq $thrown | Should Be $false
		[string]$thrown.Exception.Message | Should Match 'matrix hash does not match'
		Test-Path -LiteralPath $output | Should Be $false
		@(Get-ChildItem -LiteralPath $root -Directory -Filter 'published-output.incomplete-*').Count |
			Should Be 0
	}

	It "fails both gate entry points at baseline preflight before process or network work" {
		$root = New-IntegrityCaseRoot 'entry-point-preflight'
		$matrixPath = Join-Path $root 'matrix.json'
		$null = New-IntegrityMatrix -Path $matrixPath -CaseIds @('alpha', 'beta')
		$candidate = Join-Path $root 'stale-baseline'
		$manifestPath = New-IntegrityCandidate -Directory $candidate -CaseIds @('alpha', 'beta') `
			-MatrixHash ('f' * 64)
		$configPath = Join-Path $root 'settings.json'
		$fakeExecutable = Join-Path $root 'not-an-executable.exe'
		'{}' | Set-Content -LiteralPath $configPath -Encoding utf8NoBOM
		'not executable' | Set-Content -LiteralPath $fakeExecutable -Encoding utf8NoBOM

		$gateThrown = $null
		try {
			& $script:gateScript -AutomationPort 1 -MatrixPath $matrixPath `
				-BaselineManifestPath $manifestPath -ExpectedDevicePixelRatio 1.0 `
				-OutputDirectory (Join-Path $root 'gate-output')
		} catch { $gateThrown = $_ }
		$null -eq $gateThrown | Should Be $false
		[string]$gateThrown.Exception.Message | Should Match 'matrix hash does not match'

		$matrixThrown = $null
		try {
			& $script:matrixScript -Executable $fakeExecutable -ConfigPath $configPath `
				-MatrixPath $matrixPath -BaselineManifestPath $manifestPath `
				-OutputDirectory (Join-Path $root 'matrix-output')
		} catch { $matrixThrown = $_ }
		$null -eq $matrixThrown | Should Be $false
		[string]$matrixThrown.Exception.Message | Should Match 'matrix hash does not match'
		Test-Path -LiteralPath (Join-Path $root 'gate-output') | Should Be $false
		Test-Path -LiteralPath (Join-Path $root 'matrix-output') | Should Be $false
	}
}
