[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Root,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')]
	[string]$ExpectedArtifactName,

	[string]$ManifestPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Assert-ExactProperties {
	param([object]$Object, [string[]]$Names, [string]$Context)
	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	$expected = @($Names | Sort-Object)
	if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties."
	}
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
	$ManifestPath = Join-Path $rootPath 'draft-manifest.json'
}
$manifestItem = Get-Item -LiteralPath $ManifestPath -Force -ErrorAction Stop
$manifestFullPath = [IO.Path]::GetFullPath($manifestItem.FullName)
$manifestParent = [IO.Path]::GetDirectoryName($manifestFullPath).TrimEnd('\', '/')
if ($manifestItem.PSIsContainer -or
	($manifestItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
	-not $manifestParent.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'Draft manifest must be a regular direct child of the rehearsal root.'
}
$manifest = Read-ReleaseJson -Path $manifestItem.FullName
Assert-ExactProperties $manifest @(
	'artifactName', 'audioFree', 'createdAtUtc', 'draft', 'fileCount', 'files', 'kind', 'schemaVersion'
) 'Rehearsal draft manifest'
if ([int]$manifest.schemaVersion -ne 1 -or [string]$manifest.kind -cne 'input-enhancement-rehearsal-draft' -or
	$manifest.draft -ne $true -or $manifest.audioFree -ne $true -or
	[string]$manifest.artifactName -cne $ExpectedArtifactName) {
	throw 'Rehearsal draft manifest identity or safety flags are invalid.'
}
$createdAt = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$manifest.createdAtUtc, [Globalization.CultureInfo]::InvariantCulture,
	[Globalization.DateTimeStyles]::RoundtripKind, [ref]$createdAt)) {
	throw 'Rehearsal draft manifest createdAtUtc is invalid.'
}

$actualFiles = @(Get-ValidatedInputEnhancementRehearsalDraftFiles `
	-Root $rootPath -ExcludedPath $manifestFullPath)
$records = @($manifest.files)
if ([int]$manifest.fileCount -ne $records.Count -or $records.Count -ne $actualFiles.Count) {
	throw 'Rehearsal draft manifest file count does not match the remotely downloaded draft.'
}

$seen = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::Ordinal)
foreach ($record in $records) {
	Assert-ExactProperties $record @('path', 'sha256', 'size') 'Rehearsal draft file record'
	$relativePath = Assert-SafeRelativeReleasePath -Path ([string]$record.path) -Context 'Rehearsal draft file'
	if (-not $seen.Add($relativePath)) {
		throw "Rehearsal draft contains duplicate file '$relativePath'."
	}
	if ([string]$record.sha256 -cnotmatch '^[0-9a-f]{64}$' -or [int64]$record.size -lt 0) {
		throw "Rehearsal draft file '$relativePath' has invalid size/hash metadata."
	}
	$path = Join-Path $rootPath ($relativePath.Replace('/', '\'))
	$item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
		[int64]$record.size -ne [int64]$item.Length -or
		[string]$record.sha256 -cne (Get-ReleaseFileSha256 -Path $item.FullName)) {
		throw "Rehearsal draft file '$relativePath' failed exact remote byte verification."
	}
}

$actualRelative = @($actualFiles | ForEach-Object {
	[IO.Path]::GetRelativePath($rootPath, $_.FullName).Replace('\', '/')
})
if (@(Compare-Object -ReferenceObject @($seen | Sort-Object) -DifferenceObject @($actualRelative | Sort-Object)).Count -ne 0) {
	throw 'Remotely downloaded rehearsal draft contains an unmanifested or missing file.'
}
Write-Host "Remotely reverified all $($records.Count) rehearsal draft file(s) byte-for-byte."
