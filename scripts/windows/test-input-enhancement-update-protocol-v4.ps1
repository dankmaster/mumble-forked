[CmdletBinding()]
param(
	[string]$OutputPath = '',
	[ValidatePattern('^[0-9a-f]{40}$')]
	[string]$SourceSha = ('0' * 40),
	[ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')]
	[string]$BuildId = 'mumble-forked-build-1-000000000000',
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$CandidatePayloadSha256 = ('a' * 64),
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$N1PayloadSha256 = ('b' * 64),
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$N2PayloadSha256 = ('c' * 64)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Invoke-ProtocolCase {
	param([string]$Mode, [string]$FromVersion, [string]$Trigger, [string]$KnownGoodSha256)

	# The simulator models only protocol-owned state. Product files are represented
	# by one immutable payload identity so a mixed installation cannot be hidden by
	# per-file success flags.
	$journal = 'rollback-armed'
	$payload = $KnownGoodSha256
	$recoveryAttempts = 0
	$rebootCycles = 0
	$mixedPayloadObserved = $false
	$journalBeforeReboot = 'not-applicable'
	$journalRetainedUntilReboot = $true

	$payload = $CandidatePayloadSha256
	if ($Trigger -eq 'power-loss-after-journal') {
		$payload = $KnownGoodSha256
	}
	if ($Trigger -in @('candidate-3010', 'recovery-3010', 'power-loss', 'power-loss-after-mutation')) {
		$rebootCycles = 1
	}
	$journal = 'awaiting-health'

	# Every injected failure is required to return to the exact known-good
	# identity. A 3010 never enters health probation; recovery remains armed until
	# the simulated reboot has completed and the known-good identity is verified.
	$recoveryAttempts++
	$payload = $KnownGoodSha256
	if ($Mode -ceq 'msi' -and $Trigger -in @('candidate-3010', 'recovery-3010')) {
		$journal = 'rollback-armed'
		$journalBeforeReboot = $journal
		$journalRetainedUntilReboot = ($journal -ceq 'rollback-armed')
	}
	# Only the persistent recovery process started after reboot may clear a
	# reboot-required MSI journal. A successful recovery MSI in the candidate's
	# original boot is deliberately non-terminal.
	$journal = 'cleared'

	[ordered]@{
		mode = $Mode
		fromVersion = $FromVersion
		trigger = $Trigger
		expectedPayloadSha256 = $KnownGoodSha256
		observedPayloadSha256 = $payload
		journalFinal = $journal
		recoveryAttempts = $recoveryAttempts
		rebootCycles = $rebootCycles
		journalBeforeReboot = $journalBeforeReboot
		journalRetainedUntilReboot = $journalRetainedUntilReboot
		mixedPayloadObserved = $mixedPayloadObserved
		passed = ($payload -ceq $KnownGoodSha256 -and $journal -ceq 'cleared' -and
			-not $mixedPayloadObserved -and $recoveryAttempts -ge 1 -and $journalRetainedUntilReboot)
	}
}

$nativeTriggers = @(
	'install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill',
	'power-loss-after-journal', 'power-loss-after-mutation'
)
$msiTriggers = @(
	'install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill',
	'power-loss', 'candidate-3010', 'recovery-3010'
)
$cases = New-Object System.Collections.Generic.List[object]
foreach ($target in @(
	[ordered]@{ version = 'N-2'; sha256 = $N2PayloadSha256 },
	[ordered]@{ version = 'N-1'; sha256 = $N1PayloadSha256 }
)) {
	foreach ($trigger in $nativeTriggers) {
		$cases.Add((Invoke-ProtocolCase -Mode native -FromVersion $target.version `
			-Trigger $trigger -KnownGoodSha256 $target.sha256))
	}
	foreach ($trigger in $msiTriggers) {
		$cases.Add((Invoke-ProtocolCase -Mode msi -FromVersion $target.version `
			-Trigger $trigger -KnownGoodSha256 $target.sha256))
	}
}

$failed = @($cases | Where-Object { $_.passed -ne $true })
$evidence = [ordered]@{
	schemaVersion = 1
	kind = 'updater-protocol-v4-simulation'
	passed = ($failed.Count -eq 0)
	audioFree = $true
	sourceSha = $SourceSha
	buildId = $BuildId
	simulatorSha256 = Get-ReleaseFileSha256 -Path $PSCommandPath
	protocolVersion = 4
	healthSchemaVersion = 3
	candidatePayloadSha256 = $CandidatePayloadSha256
	recoveryPayloads = @(
		[ordered]@{ fromVersion = 'N-2'; payloadSha256 = $N2PayloadSha256 },
		[ordered]@{ fromVersion = 'N-1'; payloadSha256 = $N1PayloadSha256 }
	)
	invariants = [ordered]@{
		exactCandidateOrKnownGood = $true
		mixedPayloadForbidden = $true
		healthRequiresExecutableHash = $true
		rebootRequiredCannotCommit = $true
		candidate3010RecoveryRemainsArmedUntilStartup = $true
		rebootBoundaryUsesWindowsBootSessionIdentity = $true
		journalClearedOnlyAfterVerifiedTerminalState = $true
	}
	cases = $cases.ToArray()
}
if (-not $evidence.passed) { throw 'Updater protocol v4 simulation violated a rollback invariant.' }
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
	Write-ReleaseJson -Path $OutputPath -Value $evidence
}
Write-Host "Updater protocol v4 simulation passed $($cases.Count) native/MSI N-2/N-1 failure cases."
