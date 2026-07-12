Import-Module "$PSScriptRoot\..\QmlVisualGate.Common.psm1" -Force

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
}
