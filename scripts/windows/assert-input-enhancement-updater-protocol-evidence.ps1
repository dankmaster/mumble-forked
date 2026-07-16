[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$EvidencePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$ExpectedSourceSha,
	[Parameter(Mandatory = $true)] [ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')] [string]$ExpectedBuildId,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedCandidatePayloadSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

$evidence = Read-ReleaseJson -Path $EvidencePath
$simulatorPath = Join-Path $PSScriptRoot 'test-input-enhancement-update-protocol-v4.ps1'
if ([int]$evidence.schemaVersion -ne 1 -or [string]$evidence.kind -cne 'updater-protocol-v4-simulation' -or
	$evidence.passed -ne $true -or $evidence.audioFree -ne $true -or [int]$evidence.protocolVersion -ne 4 -or
	[int]$evidence.healthSchemaVersion -ne 3 -or [string]$evidence.sourceSha -cne $ExpectedSourceSha -or
	[string]$evidence.buildId -cne $ExpectedBuildId -or
	[string]$evidence.candidatePayloadSha256 -cne $ExpectedCandidatePayloadSha256 -or
	[string]$evidence.simulatorSha256 -cne (Get-ReleaseFileSha256 -Path $simulatorPath)) {
	throw 'Updater protocol simulation evidence is invalid or belongs to another harness/build.'
}
$invariants = $evidence.invariants
foreach ($name in @('exactCandidateOrKnownGood', 'mixedPayloadForbidden', 'healthRequiresExecutableHash',
	'rebootRequiredCannotCommit', 'candidate3010RecoveryRemainsArmedUntilStartup',
	'rebootBoundaryUsesWindowsBootSessionIdentity',
	'journalClearedOnlyAfterVerifiedTerminalState')) {
	if ($invariants.$name -ne $true) { throw "Updater protocol invariant '$name' was not proven." }
}
$recovery = @($evidence.recoveryPayloads)
$recoveryVersions = @($recovery.fromVersion | Sort-Object)
if ($recovery.Count -ne 2 -or $recoveryVersions.Count -ne 2 -or
	[string]$recoveryVersions[0] -cne 'N-1' -or [string]$recoveryVersions[1] -cne 'N-2') {
	throw 'Updater protocol simulation recovery identities are incomplete.'
}
$required = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($version in @('N-2', 'N-1')) {
	foreach ($trigger in @('install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill',
		'power-loss-after-journal', 'power-loss-after-mutation')) { $null = $required.Add("native|$version|$trigger") }
	foreach ($trigger in @('install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill',
		'power-loss', 'candidate-3010', 'recovery-3010')) { $null = $required.Add("msi|$version|$trigger") }
}
foreach ($case in @($evidence.cases)) {
	$key = "$([string]$case.mode)|$([string]$case.fromVersion)|$([string]$case.trigger)"
	if (-not $required.Remove($key) -or $case.passed -ne $true -or $case.mixedPayloadObserved -ne $false -or
		[string]$case.journalFinal -cne 'cleared' -or
		[string]$case.observedPayloadSha256 -cne [string]$case.expectedPayloadSha256) {
		throw "Updater protocol simulation case '$key' failed."
	}
	if ([string]$case.mode -ceq 'msi' -and [string]$case.trigger -in @('candidate-3010', 'recovery-3010') -and
		([int]$case.rebootCycles -lt 1 -or [string]$case.journalBeforeReboot -cne 'rollback-armed' -or
			$case.journalRetainedUntilReboot -ne $true)) {
		throw "Updater protocol simulation case '$key' cleared its MSI journal before reboot recovery."
	}
}
if ($required.Count -ne 0) { throw 'Updater protocol simulation is missing required native/MSI scenarios.' }
Write-Host 'Verified deterministic updater protocol-v4 simulation evidence.'
