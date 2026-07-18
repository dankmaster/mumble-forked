[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$ReplayLedgerRoot,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ChallengeId,
	[ValidateSet('Check', 'Reserve', 'Commit')] [string]$Operation = 'Check',
	[ValidatePattern('^[0-9a-f]{64}$')] [string]$ChallengeSha256 = '',
	[ValidatePattern('^[0-9a-f]{40}$')] [string]$SourceSha = '',
	[ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')] [string]$BuildId = '',
	[ValidatePattern('^[0-9a-f]{64}$')] [string]$ReservationId = '',
	[ValidatePattern('^[0-9a-f]{64}$')] [string]$DraftManifestSha256 = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-DurableCreateNewJson {
	param([string]$Path, [object]$Value)
	$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 6) + "`n")
	$stream = $null
	try {
		$stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
		$stream.Write($bytes, 0, $bytes.Length)
		$stream.Flush($true)
	} finally {
		if ($stream) { $stream.Dispose() }
	}
}

$ledger = Get-Item -LiteralPath $ReplayLedgerRoot -Force -ErrorAction Stop
if (-not $ledger.PSIsContainer -or ($ledger.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
	throw 'Replay ledger root must be a regular directory.'
}
$unsafe = @(Get-ChildItem -LiteralPath $ledger.FullName -Force | Where-Object {
	($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
})
if ($unsafe.Count -ne 0) { throw 'Replay ledger root contains reparse points.' }
$pendingPath = Join-Path $ledger.FullName ("$ChallengeId.pending.json")
$finalizedPath = Join-Path $ledger.FullName ("$ChallengeId.finalized.json")

if ($Operation -eq 'Check') {
	if ((Test-Path -LiteralPath $pendingPath) -or (Test-Path -LiteralPath $finalizedPath)) {
		throw 'Prepared challenge is pending or already finalized; replay refused.'
	}
	return [pscustomobject]@{ pendingPath = [IO.Path]::GetFullPath($pendingPath); finalizedPath = [IO.Path]::GetFullPath($finalizedPath) }
}

foreach ($pair in @(
	@('ChallengeSha256', $ChallengeSha256), @('SourceSha', $SourceSha), @('BuildId', $BuildId)
)) {
	if ([string]::IsNullOrWhiteSpace([string]$pair[1])) { throw "$($pair[0]) is required for replay-ledger $Operation." }
}

if ($Operation -eq 'Reserve') {
	if (Test-Path -LiteralPath $finalizedPath) { throw 'Prepared challenge has already been finalized; replay refused.' }
	if ([string]::IsNullOrWhiteSpace($ReservationId)) {
		$ReservationId = ([BitConverter]::ToString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))).Replace('-', '').ToLowerInvariant()
	}
	$document = [ordered]@{
		schemaVersion = 2
		kind = 'input-enhancement-rehearsal-challenge-consumption'
		state = 'pending'
		challengeId = $ChallengeId
		challengeSha256 = $ChallengeSha256
		sourceSha = $SourceSha
		buildId = $BuildId
		reservationId = $ReservationId
		reservedAtUtc = [datetimeoffset]::UtcNow.ToString('o')
	}
	try {
		Write-DurableCreateNewJson -Path $pendingPath -Value $document
	} catch [IO.IOException] {
		throw 'Prepared challenge is already reserved by another finalize attempt; replay refused.'
	}
	return [pscustomobject]@{
		pendingPath = [IO.Path]::GetFullPath($pendingPath)
		finalizedPath = [IO.Path]::GetFullPath($finalizedPath)
		reservationId = $ReservationId
	}
}

if ([string]::IsNullOrWhiteSpace($ReservationId) -or [string]::IsNullOrWhiteSpace($DraftManifestSha256)) {
	throw 'ReservationId and DraftManifestSha256 are required to commit replay-ledger consumption.'
}
if (Test-Path -LiteralPath $finalizedPath) { throw 'Prepared challenge has already been finalized; replay refused.' }
$pendingItem = Get-Item -LiteralPath $pendingPath -Force -ErrorAction Stop
if ($pendingItem.PSIsContainer -or ($pendingItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
	throw 'Replay-ledger pending reservation is unsafe.'
}
$pending = Get-Content -LiteralPath $pendingPath -Raw | ConvertFrom-Json
if ([int]$pending.schemaVersion -ne 2 -or [string]$pending.kind -cne 'input-enhancement-rehearsal-challenge-consumption' -or
	[string]$pending.state -cne 'pending' -or [string]$pending.challengeId -cne $ChallengeId -or
	[string]$pending.challengeSha256 -cne $ChallengeSha256 -or [string]$pending.sourceSha -cne $SourceSha -or
	[string]$pending.buildId -cne $BuildId -or [string]$pending.reservationId -cne $ReservationId) {
	throw 'Replay-ledger pending reservation does not match this finalize attempt.'
}
$finalized = [ordered]@{
	schemaVersion = 2
	kind = 'input-enhancement-rehearsal-challenge-consumption'
	state = 'finalized'
	challengeId = $ChallengeId
	challengeSha256 = $ChallengeSha256
	sourceSha = $SourceSha
	buildId = $BuildId
	reservationId = $ReservationId
	draftManifestSha256 = $DraftManifestSha256
	reservedAtUtc = [string]$pending.reservedAtUtc
	finalizedAtUtc = [datetimeoffset]::UtcNow.ToString('o')
}
try {
	Write-DurableCreateNewJson -Path $finalizedPath -Value $finalized
} catch [IO.IOException] {
	throw 'Prepared challenge was concurrently finalized; replay refused.'
}
# A crash before this delete leaves both markers, which still fails closed.
Remove-Item -LiteralPath $pendingPath -Force -ErrorAction Stop
[pscustomobject]@{ finalizedPath = [IO.Path]::GetFullPath($finalizedPath); reservationId = $ReservationId }
