[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$ReplayLedgerRoot,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ChallengeId
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ledger = Get-Item -LiteralPath $ReplayLedgerRoot -Force -ErrorAction Stop
if (-not $ledger.PSIsContainer -or ($ledger.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
	throw 'Replay ledger root must be a regular directory.'
}
$unsafe = @(Get-ChildItem -LiteralPath $ledger.FullName -Force | Where-Object {
	($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
})
if ($unsafe.Count -ne 0) { throw 'Replay ledger root contains reparse points.' }
$marker = Join-Path $ledger.FullName ("$ChallengeId.finalized.json")
if (Test-Path -LiteralPath $marker) {
	throw 'Prepared challenge has already been finalized; replay refused.'
}
[IO.Path]::GetFullPath($marker)
