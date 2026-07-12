[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
param(
	[Parameter(Mandatory = $true)][string]$CandidateDirectory,
	[Parameter(Mandatory = $true)][string]$BaselineDirectory,
	[Parameter(Mandatory = $true)][switch]$AcceptReviewedCandidates
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\QmlVisualGate.Common.psm1" -Force
if (-not $AcceptReviewedCandidates) { throw "Baseline update requires -AcceptReviewedCandidates." }
$candidate = (Resolve-Path -LiteralPath $CandidateDirectory).Path
$manifestPath = Join-Path $candidate "manifest.json"
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Assert-QmlVisualManifest $manifest | Out-Null
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
if ($PSCmdlet.ShouldProcess([IO.Path]::GetFullPath($BaselineDirectory), "Replace reviewed Qt Quick visual baseline")) {
	New-Item -ItemType Directory -Force -Path $BaselineDirectory | Out-Null
	Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $BaselineDirectory "manifest.json") -Force
	foreach ($case in @($manifest.cases)) {
		Copy-Item -LiteralPath (Join-Path $candidate "$($case.id).png") -Destination $BaselineDirectory -Force
		Copy-Item -LiteralPath (Join-Path $candidate "$($case.id).accessibility.json") -Destination $BaselineDirectory -Force
	}
}
