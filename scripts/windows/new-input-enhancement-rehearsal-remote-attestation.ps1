[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$DraftRoot,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')]
	[string]$ArtifactName,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{40}$')]
	[string]$SourceSha,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')]
	[string]$BuildId,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[1-9][0-9]*$')]
	[string]$WorkflowRunId,

	[Parameter(Mandatory = $true)]
	[string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

& (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
	-Root $DraftRoot -ExpectedArtifactName $ArtifactName
$manifestPath = Join-Path $DraftRoot 'draft-manifest.json'
$manifest = Read-ReleaseJson -Path $manifestPath
$document = [ordered]@{
	schemaVersion        = 1
	kind                 = 'input-enhancement-rehearsal-remote-reverification'
	artifactName         = $ArtifactName
	sourceSha            = $SourceSha
	buildId              = $BuildId
	workflowRunId        = $WorkflowRunId
	draftManifestSha256  = Get-ReleaseFileSha256 -Path $manifestPath
	draftManifestSize    = [int64](Get-Item -LiteralPath $manifestPath).Length
	reverifiedFileCount  = [int]$manifest.fileCount
	remoteByteReverified = $true
	createdAtUtc         = (Get-Date).ToUniversalTime().ToString('o')
}
Write-ReleaseJson -Value $document -Path $OutputPath
Write-Host "Created remote byte-reverification receipt for Actions artifact '$ArtifactName'."
