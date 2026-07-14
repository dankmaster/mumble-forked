[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
param(
	[Parameter(Mandatory = $true)][string]$CandidateDirectory,
	[Parameter(Mandatory = $true)][string]$BaselineDirectory,
	[Parameter(Mandatory = $true)][switch]$AcceptReviewedCandidates,
	[string]$MatrixPath = "$PSScriptRoot\qml-visual-gate-matrix.json"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\QmlVisualGate.Common.psm1" -Force
if (-not $AcceptReviewedCandidates) { throw "Baseline update requires -AcceptReviewedCandidates." }
$candidate = (Resolve-Path -LiteralPath $CandidateDirectory).Path
$manifestPath = Join-Path $candidate "manifest.json"
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Assert-QmlVisualManifestMatchesMatrix -Manifest $manifest -MatrixPath $MatrixPath -RequireCombinedCandidate | Out-Null
foreach ($case in @($manifest.cases)) {
	$imagePath = Join-Path $candidate "$($case.id).png"
	$accessibilityPath = Join-Path $candidate "$($case.id).accessibility.json"
	if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf) -or
		-not (Test-Path -LiteralPath $accessibilityPath -PathType Leaf)) {
		throw "Candidate artifacts for '$($case.id)' are incomplete."
	}
	$dimensions = Get-QmlVisualPngDimensions $imagePath
	if ((Get-QmlVisualFileSha256 $imagePath) -ne [string]$case.image_sha256 -or
		(Get-QmlVisualFileSha256 $accessibilityPath) -ne [string]$case.accessibility_sha256 -or
		$dimensions.width -ne [int]$case.image_width -or $dimensions.height -ne [int]$case.image_height) {
		throw "Candidate artifacts for '$($case.id)' do not match their manifest."
	}
}

$baseline = [IO.Path]::GetFullPath($BaselineDirectory)
if ($PSCmdlet.ShouldProcess($baseline, "Replace reviewed Qt Quick visual baseline")) {
	New-Item -ItemType Directory -Force -Path $baseline | Out-Null
	$expectedArtifacts = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($case in @($manifest.cases)) {
		$imageName = "$($case.id).png"
		$accessibilityName = "$($case.id).accessibility.json"
		$null = $expectedArtifacts.Add($imageName)
		$null = $expectedArtifacts.Add($accessibilityName)
		Copy-Item -LiteralPath (Join-Path $candidate $imageName) -Destination $baseline -Force
		Copy-Item -LiteralPath (Join-Path $candidate $accessibilityName) -Destination $baseline -Force
	}
	foreach ($artifact in @(Get-ChildItem -LiteralPath $baseline -File | Where-Object {
		$_.Name.EndsWith('.png', [StringComparison]::OrdinalIgnoreCase) -or
		$_.Name.EndsWith('.accessibility.json', [StringComparison]::OrdinalIgnoreCase)
	})) {
		if (-not $expectedArtifacts.Contains($artifact.Name)) {
			Remove-Item -LiteralPath $artifact.FullName -Force
		}
	}
	# Publish the manifest last so an interrupted copy cannot claim that a partial
	# artifact set is the reviewed baseline.
	Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $baseline "manifest.json") -Force
}
