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
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ChallengeId,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ChallengeSha256,

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
$challengePath = Join-Path $DraftRoot 'rehearsal-challenge.json'
if ((Get-ReleaseFileSha256 -Path $challengePath) -cne $ChallengeSha256) {
	throw 'Remote attestation challenge bytes differ from the finalized challenge binding.'
}
$challenge = Read-ReleaseJson -Path $challengePath
if ([string]$challenge.challengeId -cne $ChallengeId -or [string]$challenge.sourceSha -cne $SourceSha -or
	[string]$challenge.buildId -cne $BuildId -or [string]$challenge.phase -cne 'prepared') {
	throw 'Remote attestation challenge identity is invalid.'
}
$document = [ordered]@{
	schemaVersion        = 2
	kind                 = 'input-enhancement-rehearsal-remote-reverification'
	artifactName         = $ArtifactName
	sourceSha            = $SourceSha
	buildId              = $BuildId
	challengeId          = $ChallengeId
	challengeSha256      = $ChallengeSha256
	workflowRunId        = $WorkflowRunId
	draftManifestSha256  = Get-ReleaseFileSha256 -Path $manifestPath
	draftManifestSize    = [int64](Get-Item -LiteralPath $manifestPath).Length
	reverifiedFileCount  = [int]$manifest.fileCount
	remoteByteReverified = $true
	createdAtUtc         = (Get-Date).ToUniversalTime().ToString('o')
}
Write-ReleaseJson -Value $document -Path $OutputPath
Write-Host "Created remote byte-reverification receipt for Actions artifact '$ArtifactName'."
