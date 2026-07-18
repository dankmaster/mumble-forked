[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Executable,
	[string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'),
	[string]$OutputPath = '.tmp\community-release\candidate.json',
	[string]$CandidateId = '',
	[switch]$AllowDirtySource
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module "$PSScriptRoot\CommunityReleaseCandidate.Common.psm1" -Force

$manifest = New-CommunityReleaseCandidateManifest -RepositoryRoot $RepositoryRoot -Executable $Executable `
	-CandidateId $CandidateId -AllowDirtySource:$AllowDirtySource
$outputFile = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFile
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputFile -Encoding utf8
$manifest | ConvertTo-Json -Depth 10

if ($manifest.candidate_kind -ne 'release') {
	Write-Warning "Development candidate '$($manifest.candidate_id)' fingerprints a dirty worktree and cannot pass the final community-release gate."
}
