[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$CandidateManifestPath,
	[Parameter(Mandatory = $true)][string]$ScenarioResultsPath,
	[string]$MatrixPath = (Join-Path $PSScriptRoot 'connected-product-release-matrix.json'),
	[ValidateSet('community-candidate', 'community-release')][string]$PolicyId = 'community-candidate',
	[string]$OutputPath = '.tmp\connected-product-release-evidence.json'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'ConnectedProductGate.Common.psm1') -Force

function Get-ReleaseGateProperty {
	param(
		[AllowNull()]$Object,
		[Parameter(Mandatory = $true)][string]$Name,
		$DefaultValue = $null
	)

	if ($null -eq $Object) { return $DefaultValue }
	if ($Object -is [Collections.IDictionary]) {
		return $Object.Contains($Name) ? $Object[$Name] : $DefaultValue
	}
	$property = $Object.PSObject.Properties[$Name]
	return $property ? $property.Value : $DefaultValue
}

function Test-ReleaseGateProperty {
	param(
		[AllowNull()]$Object,
		[Parameter(Mandatory = $true)][string]$Name
	)

	if ($null -eq $Object) { return $false }
	if ($Object -is [Collections.IDictionary]) { return $Object.Contains($Name) }
	return $null -ne $Object.PSObject.Properties[$Name]
}

function Read-ReleaseGateJson {
	param([Parameter(Mandatory = $true)][string]$Path, [string]$Label = 'JSON input')

	$resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
	if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
		throw "$Label is not a file: $resolved"
	}
	try {
		$document = Get-Content -LiteralPath $resolved -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
	} catch {
		throw "$Label is not valid JSON: $resolved. $($_.Exception.Message)"
	}
	if ($null -eq $document) { throw "$Label is empty: $resolved" }
	return [pscustomobject]@{ path = $resolved; document = $document }
}

function Write-ReleaseGateJson {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)]$Document)

	$directory = Split-Path -Parent $Path
	if (-not [string]::IsNullOrWhiteSpace($directory)) {
		New-Item -ItemType Directory -Force -Path $directory | Out-Null
	}
	$temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
	try {
		$json = $Document | ConvertTo-Json -Depth 30
		[IO.File]::WriteAllText($temporaryPath, $json + [Environment]::NewLine,
			[Text.UTF8Encoding]::new($false))
		Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
	} finally {
		Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
	}
}

$candidateInput = Read-ReleaseGateJson -Path $CandidateManifestPath -Label 'Candidate manifest'
$resultsFullPath = (Resolve-Path -LiteralPath $ScenarioResultsPath -ErrorAction Stop).Path
$matrixFullPath = (Resolve-Path -LiteralPath $MatrixPath -ErrorAction Stop).Path
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..') -ErrorAction Stop).Path
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
foreach ($inputPath in @($candidateInput.path, $resultsFullPath, $matrixFullPath)) {
	if ([string]::Equals($outputFullPath, $inputPath, [StringComparison]::OrdinalIgnoreCase)) {
		throw "OutputPath must not overwrite an input manifest: $inputPath"
	}
}
if (Test-Path -LiteralPath $outputFullPath -PathType Leaf) {
	Remove-Item -LiteralPath $outputFullPath -Force
}

$candidate = $candidateInput.document
$candidateId = [string](Get-ReleaseGateProperty -Object $candidate -Name 'candidate_id' -DefaultValue '')
$candidateKind = [string](Get-ReleaseGateProperty -Object $candidate -Name 'candidate_kind' -DefaultValue '')
$source = Get-ReleaseGateProperty -Object $candidate -Name 'source'
$windows = Get-ReleaseGateProperty -Object $candidate -Name 'windows'
$sourceRevision = ([string](Get-ReleaseGateProperty -Object $source -Name 'git_sha' -DefaultValue '')).ToLowerInvariant()
$sourceCleanValue = Get-ReleaseGateProperty -Object $source -Name 'clean'
$executablePathValue = [string](Get-ReleaseGateProperty -Object $windows -Name 'executable_path' -DefaultValue '')
$expectedExecutableSha256 = ([string](
	Get-ReleaseGateProperty -Object $windows -Name 'executable_sha256' -DefaultValue '')).ToLowerInvariant()

if ([int](Get-ReleaseGateProperty -Object $candidate -Name 'schema_version' -DefaultValue 0) -ne 1 -or
	$candidateId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$' -or
	$candidateKind -notin @('development', 'release') -or
	$sourceRevision -notmatch '^[0-9a-f]{40}$' -or
	$sourceCleanValue -isnot [bool] -or
	[string]::IsNullOrWhiteSpace($executablePathValue) -or
	$expectedExecutableSha256 -notmatch '^[0-9a-f]{64}$') {
	throw 'Candidate manifest does not satisfy connected product candidate schema v1.'
}

$sourceClean = [bool]$sourceCleanValue
if ($PolicyId -eq 'community-release') {
	if ($candidateKind -ne 'release') {
		throw "community-release requires candidate_kind=release; found '$candidateKind'."
	}
	if (-not $sourceClean) {
		throw 'community-release requires source.clean=true.'
	}
}

$candidateDirectory = Split-Path -Parent $candidateInput.path
$candidateExecutablePath = $executablePathValue
if (-not [IO.Path]::IsPathRooted($candidateExecutablePath)) {
	$candidateExecutablePath = Join-Path $candidateDirectory $candidateExecutablePath
}
$candidateExecutablePath = (Resolve-Path -LiteralPath $candidateExecutablePath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $candidateExecutablePath -PathType Leaf)) {
	throw "Candidate executable is not a file: $candidateExecutablePath"
}
$actualExecutableSha256 = Get-ConnectedProductFileSha256 -Path $candidateExecutablePath
if ($actualExecutableSha256 -cne $expectedExecutableSha256) {
	throw "Candidate executable SHA-256 mismatch. Expected $expectedExecutableSha256, actual $actualExecutableSha256."
}

$resultsText = Get-Content -LiteralPath $resultsFullPath -Raw -ErrorAction Stop
if ($resultsText.TrimStart() -notmatch '^\[') {
	throw 'ScenarioResultsPath must contain a JSON array.'
}
try {
	$scenarioResults = @($resultsText | ConvertFrom-Json -ErrorAction Stop)
} catch {
	throw "Scenario results are not valid JSON: $resultsFullPath. $($_.Exception.Message)"
}

$evidence = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixFullPath `
	-CandidateId $candidateId -SourceRevision $sourceRevision -ExecutablePath $candidateExecutablePath `
	-ScenarioResults $scenarioResults -PolicyId $PolicyId -EvidenceBasePath $repositoryRoot

$evidence | Add-Member -NotePropertyName artifact_mode -NotePropertyValue 'connected_product_release_gate'
$evidence | Add-Member -NotePropertyName candidate_id -NotePropertyValue $candidateId
$evidence | Add-Member -NotePropertyName source_commit -NotePropertyValue $sourceRevision
$evidence | Add-Member -NotePropertyName executable_sha256 -NotePropertyValue $actualExecutableSha256
$evidence.candidate['kind'] = $candidateKind
$evidence.candidate['source_clean'] = $sourceClean
$evidence | Add-Member -NotePropertyName inputs -NotePropertyValue ([ordered]@{
	candidate_manifest = [ordered]@{
		file_name = [IO.Path]::GetFileName($candidateInput.path)
		sha256 = Get-ConnectedProductFileSha256 -Path $candidateInput.path
	}
	scenario_results = [ordered]@{
		file_name = [IO.Path]::GetFileName($resultsFullPath)
		sha256 = Get-ConnectedProductFileSha256 -Path $resultsFullPath
	}
})

Write-ReleaseGateJson -Path $outputFullPath -Document $evidence
$evidence | ConvertTo-Json -Depth 30

if (-not $evidence.eligible) {
	exit 1
}
