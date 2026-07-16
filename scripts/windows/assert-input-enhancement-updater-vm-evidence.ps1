[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$EvidencePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$ExpectedSourceSha,
	[Parameter(Mandatory = $true)] [ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')] [string]$ExpectedBuildId,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedCandidatePayloadSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedCandidateInstallerSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedCandidateExecutableSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedHarnessSha256,
	[Parameter(Mandatory = $true)] [string]$ReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedReceiptSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedVmExecutorSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedImageSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedSnapshotSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedHardwareFingerprintSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Assert-ExactProperties([object]$Object, [string[]]$Names, [string]$Context) {
	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	if (@(Compare-Object -ReferenceObject @($Names | Sort-Object) -DifferenceObject $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties."
	}
}

$evidence = Read-ReleaseJson -Path $EvidencePath
$actualEvidenceSha256 = Get-ReleaseFileSha256 -Path $EvidencePath
$actualReceiptSha256 = Get-ReleaseFileSha256 -Path $ReceiptPath
if ($actualReceiptSha256 -cne $ExpectedReceiptSha256) {
	throw 'Updater VM receipt does not match its protected SHA-256 input.'
}
$receipt = Read-ReleaseJson -Path $ReceiptPath
Assert-ExactProperties $receipt @(
	'audioFree', 'buildId', 'createdAtUtc', 'evidenceSha256', 'hardwareFingerprintSha256', 'imageSha256',
	'kind', 'passed', 'schemaVersion', 'snapshotSha256', 'sourceSha', 'vmExecutorSha256'
) 'Updater VM protected receipt'
if ([int]$receipt.schemaVersion -ne 1 -or [string]$receipt.kind -cne 'updater-v4-protected-vm-receipt' -or
	$receipt.passed -ne $true -or $receipt.audioFree -ne $true -or
	[string]$receipt.sourceSha -cne $ExpectedSourceSha -or [string]$receipt.buildId -cne $ExpectedBuildId -or
	[string]$receipt.evidenceSha256 -cne $actualEvidenceSha256 -or
	[string]$receipt.vmExecutorSha256 -cne $ExpectedVmExecutorSha256 -or
	[string]$receipt.imageSha256 -cne $ExpectedImageSha256 -or
	[string]$receipt.snapshotSha256 -cne $ExpectedSnapshotSha256 -or
	[string]$receipt.hardwareFingerprintSha256 -cne $ExpectedHardwareFingerprintSha256) {
	throw 'Updater VM receipt is invalid or does not bind the protected executor/image/snapshot/hardware and evidence bytes.'
}
$receiptCreatedAt = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$receipt.createdAtUtc,
	[Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind,
	[ref]$receiptCreatedAt)) { throw 'Updater VM receipt timestamp is invalid.' }

Assert-ExactProperties $evidence @(
	'audioFree', 'buildId', 'candidate', 'cases', 'createdAtUtc', 'kind', 'passed', 'recoveryTargets',
	'runner', 'schemaVersion', 'sourceSha'
) 'Updater VM evidence'
if ([int]$evidence.schemaVersion -ne 1 -or [string]$evidence.kind -cne 'updater-v4-vm-rollback-matrix' -or
	$evidence.passed -ne $true -or $evidence.audioFree -ne $true -or
	[string]$evidence.sourceSha -cne $ExpectedSourceSha -or [string]$evidence.buildId -cne $ExpectedBuildId) {
	throw 'Updater VM evidence identity/status is invalid.'
}
$createdAt = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$evidence.createdAtUtc,
	[Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind,
	[ref]$createdAt)) { throw 'Updater VM evidence timestamp is invalid.' }

Assert-ExactProperties $evidence.candidate @('executableSha256', 'installerSha256', 'payloadSha256') 'Updater VM candidate'
if ([string]$evidence.candidate.payloadSha256 -cne $ExpectedCandidatePayloadSha256 -or
	[string]$evidence.candidate.installerSha256 -cne $ExpectedCandidateInstallerSha256 -or
	[string]$evidence.candidate.executableSha256 -cne $ExpectedCandidateExecutableSha256) {
	throw 'Updater VM evidence tested different candidate bytes.'
}
Assert-ExactProperties $evidence.runner @(
	'class', 'hardwareFingerprintSha256', 'harnessSha256', 'imageSha256', 'isolated', 'snapshotSha256',
	'vmExecutorSha256'
) 'Updater VM runner'
if ([string]$evidence.runner.class -cne 'protected-windows-update-vm' -or $evidence.runner.isolated -ne $true -or
	[string]$evidence.runner.harnessSha256 -cne $ExpectedHarnessSha256 -or
	[string]$evidence.runner.vmExecutorSha256 -cne $ExpectedVmExecutorSha256 -or
	[string]$evidence.runner.imageSha256 -cne $ExpectedImageSha256 -or
	[string]$evidence.runner.snapshotSha256 -cne $ExpectedSnapshotSha256 -or
	[string]$evidence.runner.hardwareFingerprintSha256 -cne $ExpectedHardwareFingerprintSha256) {
	throw 'Updater VM evidence did not use the protected isolated runner/harness.'
}
foreach ($name in @('hardwareFingerprintSha256', 'harnessSha256', 'imageSha256', 'snapshotSha256', 'vmExecutorSha256')) {
	if ([string]$evidence.runner.$name -cnotmatch '^[0-9a-f]{64}$') { throw "Updater VM runner $name is invalid." }
}

$targets = @($evidence.recoveryTargets)
if ($targets.Count -ne 2) { throw 'Updater VM evidence requires exact N-2 and N-1 recovery targets.' }
$targetByVersion = @{}
foreach ($target in $targets) {
	Assert-ExactProperties $target @('buildId', 'fromVersion', 'installerSha256', 'payloadSha256') 'Updater VM recovery target'
	if ([string]$target.fromVersion -cnotin @('N-2', 'N-1') -or
		$targetByVersion.ContainsKey([string]$target.fromVersion) -or
		[string]$target.buildId -cnotmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' -or
		[string]$target.installerSha256 -cnotmatch '^[0-9a-f]{64}$' -or
		[string]$target.payloadSha256 -cnotmatch '^[0-9a-f]{64}$') {
		throw 'Updater VM recovery target is invalid or duplicated.'
	}
	$targetByVersion[[string]$target.fromVersion] = $target
}
if ($targetByVersion.Count -ne 2) { throw 'Updater VM recovery target set is incomplete.' }

$nativeTriggers = @('install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill',
	'power-loss-after-journal', 'power-loss-after-mutation')
$msiTriggers = @('install-failure', 'crash-before-marker', 'audio-init-failure', 'process-kill',
	'power-loss', 'candidate-3010', 'recovery-3010')
$required = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($version in @('N-2', 'N-1')) {
	foreach ($trigger in $nativeTriggers) { $null = $required.Add("native|$version|$trigger") }
	foreach ($trigger in $msiTriggers) { $null = $required.Add("msi|$version|$trigger") }
}
$cases = @($evidence.cases)
if ($cases.Count -ne $required.Count) { throw "Updater VM matrix requires exactly $($required.Count) cases." }
foreach ($case in $cases) {
	Assert-ExactProperties $case @(
		'exitCode', 'fromVersion', 'journalFinal', 'mixedPayloadObserved', 'mode', 'observedPayloadSha256',
		'passed', 'rebootCycles', 'residualJournalCount', 'residualManagedFileCount', 'trigger'
	) 'Updater VM case'
	$key = "$([string]$case.mode)|$([string]$case.fromVersion)|$([string]$case.trigger)"
	if (-not $required.Remove($key) -or $case.passed -ne $true -or $case.mixedPayloadObserved -ne $false -or
		[int]$case.residualJournalCount -ne 0 -or [int]$case.residualManagedFileCount -ne 0 -or
		[string]$case.journalFinal -cne 'cleared' -or [int]$case.rebootCycles -lt 0 -or
		[string]$case.observedPayloadSha256 -cne [string]$targetByVersion[[string]$case.fromVersion].payloadSha256) {
		throw "Updater VM case '$key' failed exact-known-good or cleanup invariants."
	}
}
if ($required.Count -ne 0) { throw 'Updater VM rollback matrix is missing required scenarios.' }
Write-Host "Verified protected updater VM rollback matrix with $($cases.Count) native/MSI N-2/N-1 cases."
