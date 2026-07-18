[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$SourceRoot,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$SourceSha,
	[Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int]$BuildNumber,
	[Parameter(Mandatory = $true)] [string]$PreparedRoot,
	[Parameter(Mandatory = $true)] [string]$ChallengePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ChallengeSha256,
	[Parameter(Mandatory = $true)] [string]$FinalizeExecutorPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$FinalizeExecutorSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$PrepareExecutorSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UnsignedHandoffArchiveSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$MeasuredEvidenceArchiveSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ListeningQualificationSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ReleaseSmokeHarnessSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$FixtureManifestSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$CaseSetSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ServerExecutableSha256,
	[Parameter(Mandatory = $true)] [string]$KillSwitchObserverPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$KillSwitchObserverSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')] [string]$KillSwitchObserverIdentity,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$KillSwitchObserverPublicKeyHex,
	[Parameter(Mandatory = $true)] [string]$KillSwitchRuntimeTracePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$KillSwitchRuntimeTraceSha256,
	[Parameter(Mandatory = $true)] [string]$KillSwitchObserverReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$KillSwitchObserverReceiptSha256,
	[Parameter(Mandatory = $true)] [string]$UpdaterVmExecutorPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmExecutorSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')] [string]$UpdaterVmObserverIdentity,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmObserverPublicKeyHex,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmHarnessSha256,
	[Parameter(Mandatory = $true)] [string]$UpdaterVmEvidencePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmEvidenceSha256,
	[Parameter(Mandatory = $true)] [string]$UpdaterVmReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmReceiptSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmImageSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmSnapshotSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmHardwareFingerprintSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')] [string]$DraftArtifactName,
	[Parameter(Mandatory = $true)] [string]$AllowedOutputParent,
	[Parameter(Mandatory = $true)] [string]$OutputRoot,
	[Parameter(Mandatory = $true)] [string]$ReplayLedgerRoot,
	[ValidateRange(0, 1000)] [int]$CommunitySize = 0,
	[string]$OpenSslPath = '',
	[string]$DumpbinPath = '',
	[string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'Release rehearsal finalize is Windows-only.' }
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\', '/')
$preparedRootPath = (Resolve-Path -LiteralPath $PreparedRoot).Path.TrimEnd('\', '/')
$challengeItem = Get-Item -LiteralPath $ChallengePath -Force -ErrorAction Stop
if ($challengeItem.PSIsContainer -or ($challengeItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
	(Get-ReleaseFileSha256 -Path $challengeItem.FullName) -cne $ChallengeSha256) {
	throw 'Finalize received a missing, unsafe, or changed rehearsal challenge.'
}
$preparedPrefix = $preparedRootPath + [IO.Path]::DirectorySeparatorChar
if (-not [IO.Path]::GetFullPath($challengeItem.FullName).StartsWith($preparedPrefix,
	[StringComparison]::OrdinalIgnoreCase)) {
	throw 'Finalize challenge must be contained by the prepared root.'
}
if ((git -C $sourceRootPath rev-parse HEAD).Trim() -cne $SourceSha) {
	throw 'Finalize source checkout differs from the requested immutable commit.'
}

function Resolve-ProtectedFile {
	param([string]$Label, [string]$Path, [string]$Sha256)
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
		(Get-ReleaseFileSha256 -Path $item.FullName) -cne $Sha256) {
		throw "Protected $Label is unsafe or differs from its pinned SHA-256."
	}
	$resolved = [IO.Path]::GetFullPath($item.FullName)
	foreach ($forbiddenRoot in @($sourceRootPath, $preparedRootPath)) {
		$prefix = $forbiddenRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
		if ($resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
			throw "Protected independent $Label must live outside source and prepared roots."
		}
	}
	return $resolved
}

$inputs = [ordered]@{
	finalizeExecutor = Resolve-ProtectedFile 'finalize executor' $FinalizeExecutorPath $FinalizeExecutorSha256
	killObserver = Resolve-ProtectedFile 'kill-switch observer' $KillSwitchObserverPath $KillSwitchObserverSha256
	killTrace = Resolve-ProtectedFile 'kill-switch runtime trace' $KillSwitchRuntimeTracePath $KillSwitchRuntimeTraceSha256
	killReceipt = Resolve-ProtectedFile 'kill-switch observer receipt' $KillSwitchObserverReceiptPath $KillSwitchObserverReceiptSha256
	vmExecutor = Resolve-ProtectedFile 'updater VM executor' $UpdaterVmExecutorPath $UpdaterVmExecutorSha256
	vmEvidence = Resolve-ProtectedFile 'updater VM evidence' $UpdaterVmEvidencePath $UpdaterVmEvidenceSha256
	vmReceipt = Resolve-ProtectedFile 'updater VM receipt' $UpdaterVmReceiptPath $UpdaterVmReceiptSha256
}
if ([IO.Path]::GetExtension($inputs.finalizeExecutor) -cne '.ps1') {
	throw 'Protected finalize executor must be a PowerShell script.'
}
$executorSource = Get-Content -LiteralPath $inputs.finalizeExecutor -Raw
foreach ($forbiddenPattern in @(
	'(?i)\bgh\s+release\b', '(?i)api\.github\.com/.*/releases', '(?i)artifact[- ]?signing',
	'(?i)trusted[- ]?signing', '(?i)\bazure\b', '(?i)contents\s*:\s*write'
)) {
	if ($executorSource -match $forbiddenPattern) {
		throw "Protected finalize executor contains forbidden capability '$forbiddenPattern'."
	}
}

$buildId = Get-InputEnhancementBuildId -BuildNumber $BuildNumber -SourceSha $SourceSha
if ($KillSwitchObserverIdentity -ceq $UpdaterVmObserverIdentity -or
	$KillSwitchObserverPublicKeyHex -ceq $UpdaterVmObserverPublicKeyHex) {
	throw 'Kill-switch and updater-VM observers must use distinct identities and observer-held keys.'
}
$challengeResult = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-challenge.ps1') `
	-PreparedRoot $preparedRootPath -ChallengePath $challengeItem.FullName `
	-ExpectedSourceSha $SourceSha -ExpectedBuildId $buildId `
	-ExpectedPrepareExecutorSha256 $PrepareExecutorSha256 `
	-ExpectedUnsignedHandoffSha256 $UnsignedHandoffArchiveSha256 `
	-ExpectedMeasuredEvidenceSha256 $MeasuredEvidenceArchiveSha256 `
	-ExpectedListeningQualificationSha256 $ListeningQualificationSha256 `
	-ExpectedReleaseSmokeHarnessSha256 $ReleaseSmokeHarnessSha256 `
	-ExpectedFixtureManifestSha256 $FixtureManifestSha256 `
	-ExpectedCaseSetSha256 $CaseSetSha256 `
	-ExpectedServerExecutableSha256 $ServerExecutableSha256 -RequireCanonicalJson
$challengeId = [string]$challengeResult.challengeId

$ledgerItem = Get-Item -LiteralPath $ReplayLedgerRoot -Force -ErrorAction Stop
if (-not $ledgerItem.PSIsContainer -or ($ledgerItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
	throw 'Replay ledger root must be a regular directory.'
}
$ledgerRootPath = [IO.Path]::GetFullPath($ledgerItem.FullName).TrimEnd('\', '/')
foreach ($forbiddenRoot in @($sourceRootPath, $preparedRootPath, [IO.Path]::GetFullPath($AllowedOutputParent).TrimEnd('\', '/'))) {
	if ($ledgerRootPath.Equals($forbiddenRoot, [StringComparison]::OrdinalIgnoreCase) -or
		$ledgerRootPath.StartsWith(($forbiddenRoot + '\'), [StringComparison]::OrdinalIgnoreCase) -or
		$forbiddenRoot.StartsWith(($ledgerRootPath + '\'), [StringComparison]::OrdinalIgnoreCase)) {
		throw 'Replay ledger must not overlap source, prepared, or draft roots.'
	}
}
$null = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-replay.ps1') `
	-ReplayLedgerRoot $ledgerRootPath -ChallengeId $challengeId -Operation Check

& (Join-Path $PSScriptRoot 'assert-input-enhancement-updater-vm-evidence.ps1') `
	-EvidencePath $inputs.vmEvidence -ReceiptPath $inputs.vmReceipt `
	-ExpectedReceiptSha256 $UpdaterVmReceiptSha256 `
	-ExpectedSourceSha $SourceSha -ExpectedBuildId $buildId -ExpectedChallengeId $challengeId `
	-ExpectedCandidatePayloadSha256 ([string]$challengeResult.signedStagedPayloadSha256) `
	-ExpectedCandidateInstallerSha256 ([string]$challengeResult.installerSha256) `
	-ExpectedCandidateExecutableSha256 ([string]$challengeResult.signedTestedBinarySha256) `
	-ExpectedHarnessSha256 $UpdaterVmHarnessSha256 `
	-ExpectedVmExecutorSha256 $UpdaterVmExecutorSha256 `
	-ExpectedObserverIdentity $UpdaterVmObserverIdentity `
	-ExpectedObserverPublicKeyHex $UpdaterVmObserverPublicKeyHex `
	-ExpectedImageSha256 $UpdaterVmImageSha256 -ExpectedSnapshotSha256 $UpdaterVmSnapshotSha256 `
	-ExpectedHardwareFingerprintSha256 $UpdaterVmHardwareFingerprintSha256 -OpenSslPath $OpenSslPath

$challengeDocument = Read-ReleaseJson -Path $challengeItem.FullName
& (Join-Path $PSScriptRoot 'assert-input-enhancement-kill-switch-observation.ps1') `
	-RuntimeTracePath $inputs.killTrace -ReceiptPath $inputs.killReceipt `
	-ExpectedReceiptSha256 $KillSwitchObserverReceiptSha256 `
	-ExpectedSourceSha $SourceSha -ExpectedBuildId $buildId -ExpectedChallengeId $challengeId `
	-ExpectedObserverSha256 $KillSwitchObserverSha256 `
	-ExpectedObserverIdentity $KillSwitchObserverIdentity `
	-ExpectedObserverPublicKeyHex $KillSwitchObserverPublicKeyHex `
	-ExpectedTestedBinarySha256 ([string]$challengeResult.signedTestedBinarySha256) `
	-ExpectedStagedPayloadSha256 ([string]$challengeResult.signedStagedPayloadSha256) `
	-ExpectedPolicySha256 ([string]$challengeDocument.signed.policySha256) -OpenSslPath $OpenSslPath

# Atomically and durably acquire ownership before creating an output tree or
# invoking the finalize executor. Any crash from here leaves a pending marker
# that blocks retries until an operator audits the abandoned attempt.
$reservation = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-replay.ps1') `
	-ReplayLedgerRoot $ledgerRootPath -ChallengeId $challengeId -Operation Reserve `
	-ChallengeSha256 $ChallengeSha256 -SourceSha $SourceSha -BuildId $buildId

$outputRootPath = Initialize-InputEnhancementRehearsalOutputRoot `
	-OutputRoot $OutputRoot -AllowedOutputParent $AllowedOutputParent -SourceRoot $sourceRootPath
& $inputs.finalizeExecutor `
	-Operation Finalize -SourceRoot $sourceRootPath -SourceSha $SourceSha -BuildNumber $BuildNumber `
	-ChallengeId $challengeId -ChallengePath $challengeItem.FullName -ChallengeSha256 $ChallengeSha256 `
	-PreparedRoot $preparedRootPath `
	-KillSwitchRuntimeTracePath $inputs.killTrace `
	-KillSwitchObserverReceiptPath $inputs.killReceipt `
	-UpdaterVmEvidencePath $inputs.vmEvidence -UpdaterVmReceiptPath $inputs.vmReceipt `
	-FinalizeExecutorSha256 $FinalizeExecutorSha256 `
	-DraftArtifactName $DraftArtifactName -OutputRoot $outputRootPath
if (-not $?) { throw 'Protected finalize executor returned failure.' }
if ((Get-ReleaseFileSha256 -Path $challengeItem.FullName) -cne $ChallengeSha256) {
	throw 'Prepared challenge changed during finalize.'
}

& (Join-Path $PSScriptRoot 'assert-input-enhancement-release-rehearsal.ps1') `
	-Root $outputRootPath -ExpectedSourceSha $SourceSha -ExpectedBuildId $buildId `
	-ExpectedDraftArtifactName $DraftArtifactName `
	-ExpectedEd25519PublicKeyHex ([string]$challengeResult.ed25519PublicKeyHex) `
	-ExpectedPrepareExecutorSha256 $PrepareExecutorSha256 `
	-ExpectedFinalizeExecutorSha256 $FinalizeExecutorSha256 `
	-ExpectedChallengeId $challengeId -ExpectedChallengeSha256 $ChallengeSha256 `
	-ExpectedCandidateBuildReceiptSha256 ([string]$challengeResult.candidateBuildReceiptSha256) `
	-ExpectedUnsignedTestedBinarySha256 ([string]$challengeResult.unsignedTestedBinarySha256) `
	-ExpectedUnsignedStagedPayloadSha256 ([string]$challengeResult.unsignedStagedPayloadSha256) `
	-ExpectedSignedTestedBinarySha256 ([string]$challengeResult.signedTestedBinarySha256) `
	-ExpectedSignedStagedPayloadSha256 ([string]$challengeResult.signedStagedPayloadSha256) `
	-ExpectedUnsignedHandoffSha256 $UnsignedHandoffArchiveSha256 `
	-ExpectedMeasuredEvidenceArchiveSha256 $MeasuredEvidenceArchiveSha256 `
	-ExpectedListeningQualificationSha256 $ListeningQualificationSha256 `
	-ExpectedReleaseSmokeHarnessSha256 $ReleaseSmokeHarnessSha256 `
	-ExpectedFixtureManifestSha256 $FixtureManifestSha256 `
	-ExpectedCaseSetSha256 $CaseSetSha256 -ExpectedServerExecutableSha256 $ServerExecutableSha256 `
	-ExpectedKillSwitchObserverSha256 $KillSwitchObserverSha256 `
	-ExpectedKillSwitchObserverReceiptSha256 $KillSwitchObserverReceiptSha256 `
	-ExpectedKillSwitchObserverIdentity $KillSwitchObserverIdentity `
	-ExpectedKillSwitchObserverPublicKeyHex $KillSwitchObserverPublicKeyHex `
	-ExpectedUpdaterVmExecutorSha256 $UpdaterVmExecutorSha256 `
	-ExpectedUpdaterVmObserverIdentity $UpdaterVmObserverIdentity `
	-ExpectedUpdaterVmObserverPublicKeyHex $UpdaterVmObserverPublicKeyHex `
	-ExpectedUpdaterVmHarnessSha256 $UpdaterVmHarnessSha256 `
	-ExpectedUpdaterVmReceiptSha256 $UpdaterVmReceiptSha256 `
	-ExpectedUpdaterVmImageSha256 $UpdaterVmImageSha256 `
	-ExpectedUpdaterVmSnapshotSha256 $UpdaterVmSnapshotSha256 `
	-ExpectedUpdaterVmHardwareFingerprintSha256 $UpdaterVmHardwareFingerprintSha256 `
	-ExpectedCommunitySize $CommunitySize -OpenSslPath $OpenSslPath -DumpbinPath $DumpbinPath `
	-PythonPath $PythonPath

& (Join-Path $PSScriptRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
	-Root $outputRootPath -ArtifactName $DraftArtifactName
& (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
	-Root $outputRootPath -ExpectedArtifactName $DraftArtifactName

$draftManifestPath = Join-Path $outputRootPath 'draft-manifest.json'
$null = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-replay.ps1') `
	-ReplayLedgerRoot $ledgerRootPath -ChallengeId $challengeId -Operation Commit `
	-ChallengeSha256 $ChallengeSha256 -SourceSha $SourceSha -BuildId $buildId `
	-ReservationId ([string]$reservation.reservationId) `
	-DraftManifestSha256 (Get-ReleaseFileSha256 -Path $draftManifestPath)

Write-Host "Finalized challenge '$challengeId' as local draft '$DraftArtifactName'; replay marker committed."
