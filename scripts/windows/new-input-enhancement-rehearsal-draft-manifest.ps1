[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Root,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')]
	[string]$ArtifactName,

	[string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
	$OutputPath = Join-Path $rootPath 'draft-manifest.json'
}
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$outputParent = [IO.Path]::GetDirectoryName($outputFullPath).TrimEnd('\', '/')
if (-not $outputParent.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'Draft manifest must be written directly inside the rehearsal root.'
}

$files = @(Get-ValidatedInputEnhancementRehearsalDraftFiles `
	-Root $rootPath -ExcludedPath $outputFullPath)
$records = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) {
	$relativePath = [IO.Path]::GetRelativePath($rootPath, $file.FullName).Replace('\', '/')
	$records.Add([ordered]@{
		path   = $relativePath
		sha256 = Get-ReleaseFileSha256 -Path $file.FullName
		size   = [int64]$file.Length
	})
}
if ($records.Count -eq 0) {
	throw 'Rehearsal draft is empty.'
}

$document = [ordered]@{
	schemaVersion = 1
	kind          = 'input-enhancement-rehearsal-draft'
	artifactName  = $ArtifactName
	draft         = $true
	audioFree     = $true
	createdAtUtc  = (Get-Date).ToUniversalTime().ToString('o')
	fileCount     = $records.Count
	files         = $records.ToArray()
}
Write-ReleaseJson -Value $document -Path $outputFullPath
Write-Host "Created immutable local rehearsal draft manifest for $($records.Count) file(s)."
