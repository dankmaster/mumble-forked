[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Root,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{40}$')]
	[string]$ExpectedSourceSha,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')]
	[string]$ExpectedBuildId,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')]
	[string]$ExpectedDraftArtifactName,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedExecutorSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedUnsignedHandoffSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedMeasuredEvidenceArchiveSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedListeningQualificationSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedReleaseSmokeHarnessSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedFixtureManifestSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedCaseSetSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedServerExecutableSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedKillSwitchObserverSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedKillSwitchObserverReceiptSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedUpdaterVmExecutorSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedUpdaterVmReceiptSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedUpdaterVmImageSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedUpdaterVmSnapshotSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedUpdaterVmHardwareFingerprintSha256,

	[ValidateRange(0, 1000)]
	[int]$ExpectedCommunitySize = 0,

	[string]$OpenSslPath = '',

	[string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Assert-ExactProperties {
	param([object]$Object, [string[]]$Names, [string]$Context)
	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	$expected = @($Names | Sort-Object)
	if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties."
	}
}

function Assert-Sha256 {
	param([object]$Value, [string]$Context)
	$text = [string]$Value
	if ($text -cnotmatch '^[0-9a-f]{64}$') { throw "$Context is not a lowercase SHA-256." }
	return $text
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$rehearsalPath = Join-Path $rootPath 'rehearsal.json'
$rehearsal = Read-ReleaseJson -Path $rehearsalPath
Assert-ExactProperties $rehearsal @(
	'artifacts', 'binding', 'buildId', 'createdAtUtc', 'draft', 'ephemeralSigning', 'killSwitch', 'kind',
	'passed', 'releaseMatrix', 'schemaVersion', 'security', 'sequence', 'sourceSha'
) 'Release rehearsal'
if ([int]$rehearsal.schemaVersion -ne 1 -or
	[string]$rehearsal.kind -cne 'input-enhancement-pre-azure-release-rehearsal' -or
	$rehearsal.passed -ne $true -or $rehearsal.draft -ne $true -or
	[string]$rehearsal.sourceSha -cne $ExpectedSourceSha -or
	[string]$rehearsal.buildId -cne $ExpectedBuildId) {
	throw 'Release rehearsal identity/status is invalid.'
}
$createdAt = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$rehearsal.createdAtUtc, [Globalization.CultureInfo]::InvariantCulture,
	[Globalization.DateTimeStyles]::RoundtripKind, [ref]$createdAt)) {
	throw 'Release rehearsal createdAtUtc is invalid.'
}

$security = $rehearsal.security
Assert-ExactProperties $security @(
	'azureUsed', 'contentsWrite', 'githubReleaseCreated', 'privateMaterialIncluded', 'productionCredentialsUsed',
	'publication', 'remoteArtifactName', 'remoteStore'
) 'Release rehearsal security'
if ($security.azureUsed -ne $false -or $security.contentsWrite -ne $false -or
	$security.githubReleaseCreated -ne $false -or $security.privateMaterialIncluded -ne $false -or
	$security.productionCredentialsUsed -ne $false -or [string]$security.publication -cne 'local-draft' -or
	[string]$security.remoteStore -cne 'github-actions-artifact' -or
	[string]$security.remoteArtifactName -cne $ExpectedDraftArtifactName) {
	throw 'Release rehearsal used a production/Azure/publication path or the wrong draft artifact store.'
}

$signing = $rehearsal.ephemeralSigning
Assert-ExactProperties $signing @(
	'certificateSubject', 'certificateThumbprint', 'ed25519PublicKeyHex', 'testOnly', 'timestampMode'
) 'Release rehearsal ephemeral signing'
$ed25519PublicKey = Assert-Ed25519PublicKeyHex -PublicKeyHex ([string]$signing.ed25519PublicKeyHex)
if ($signing.testOnly -ne $true -or
	[string]$signing.certificateSubject -cnotmatch '^CN=Mumble Input Enhancement Rehearsal [A-Za-z0-9._-]+$' -or
	[string]$signing.certificateThumbprint -cnotmatch '^[0-9A-Fa-f]{40}$' -or
	[string]$signing.timestampMode -cne 'test-rfc3161') {
	throw 'Release rehearsal must use only its ephemeral test certificate and test Ed25519 key.'
}

$binding = $rehearsal.binding
Assert-ExactProperties $binding @(
	'caseSetSha256', 'corpusInventorySha256', 'executorSha256', 'fixtureManifestSha256',
	'listeningInputSha256', 'measuredEvidenceArchiveSha256', 'mixturePlanSha256', 'modelManifestSha256',
	'recipeManifestSha256', 'recipeSetVersion', 'releaseSmokeHarnessSha256', 'serverExecutableSha256',
	'stagedPayloadSha256', 'testedBinarySha256', 'unsignedHandoffSha256'
) 'Release rehearsal binding'
foreach ($name in @(
	'caseSetSha256', 'corpusInventorySha256', 'executorSha256', 'fixtureManifestSha256',
	'listeningInputSha256', 'measuredEvidenceArchiveSha256', 'mixturePlanSha256', 'modelManifestSha256',
	'recipeManifestSha256', 'releaseSmokeHarnessSha256', 'serverExecutableSha256', 'stagedPayloadSha256',
	'testedBinarySha256', 'unsignedHandoffSha256'
)) { $null = Assert-Sha256 $binding.$name "Release rehearsal binding $name" }
$expectedBinding = [ordered]@{
	executorSha256                = $ExpectedExecutorSha256
	unsignedHandoffSha256         = $ExpectedUnsignedHandoffSha256
	measuredEvidenceArchiveSha256 = $ExpectedMeasuredEvidenceArchiveSha256
	listeningInputSha256          = $ExpectedListeningQualificationSha256
	releaseSmokeHarnessSha256     = $ExpectedReleaseSmokeHarnessSha256
	fixtureManifestSha256         = $ExpectedFixtureManifestSha256
	caseSetSha256                 = $ExpectedCaseSetSha256
	serverExecutableSha256        = $ExpectedServerExecutableSha256
}
foreach ($name in $expectedBinding.Keys) {
	if ([string]$binding.$name -cne [string]$expectedBinding[$name]) {
		throw "Release rehearsal binding '$name' does not match its protected input."
	}
}
if ([string]$binding.recipeSetVersion -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
	throw 'Release rehearsal recipe-set version is invalid.'
}

$expectedSequence = @(
	'verify-pre-azure-inputs', 'sign-pe-ephemeral', 'build-msi-from-signed-payload', 'sign-msi-ephemeral',
	'assemble-immutable-artifacts', 'qualify-core', 'release-smoke-6-plus-24',
	'simulate-updater-protocol-v4', 'verify-updater-vm-rollback-matrix', 'stage-local-draft',
	'exercise-policy-kill-switch'
)
$sequence = @($rehearsal.sequence)
if ($sequence.Count -ne $expectedSequence.Count) { throw 'Release rehearsal sequence is incomplete.' }
for ($index = 0; $index -lt $sequence.Count; ++$index) {
	Assert-ExactProperties $sequence[$index] @('name', 'passed') "Release rehearsal sequence[$index]"
	if ([string]$sequence[$index].name -cne $expectedSequence[$index] -or $sequence[$index].passed -ne $true) {
		throw "Release rehearsal sequence step $index is missing, reordered, or failed."
	}
}

$matrix = $rehearsal.releaseMatrix
Assert-ExactProperties $matrix @('enhancedCases', 'originalControls', 'receiverCleanupEnabled') 'Release rehearsal matrix'
if ([int]$matrix.originalControls -ne 6 -or [int]$matrix.enhancedCases -ne 24 -or
	$matrix.receiverCleanupEnabled -ne $false) {
	throw 'Release rehearsal must contain exactly 6 Original controls and 24 enhanced cases with receiver cleanup off.'
}

$killSwitch = $rehearsal.killSwitch
Assert-ExactProperties $killSwitch @(
	'activeProfileAfter', 'activeProfileBefore', 'maxJitterSeconds', 'observedForceOriginalSeconds', 'passed',
	'refreshIntervalSeconds', 'runtimeTraceSha256', 'startupChecked'
) 'Release rehearsal kill switch'
if ($killSwitch.startupChecked -ne $true -or $killSwitch.passed -ne $true -or
	[int]$killSwitch.refreshIntervalSeconds -ne 900 -or [int]$killSwitch.maxJitterSeconds -lt 0 -or
	([int]$killSwitch.refreshIntervalSeconds + [int]$killSwitch.maxJitterSeconds) -gt 1200 -or
	[double]$killSwitch.observedForceOriginalSeconds -lt 0 -or
	[double]$killSwitch.observedForceOriginalSeconds -gt 1200 -or
	[string]$killSwitch.runtimeTraceSha256 -cnotmatch '^[0-9a-f]{64}$' -or
	[string]$killSwitch.activeProfileAfter -cne 'Original') {
	throw 'Policy-only kill switch did not prove startup checking and transition to Original within 20 minutes.'
}

$requiredAssetNames = @(
	'auditArtifact', 'caseSet', 'channelPointer', 'channelPointerSignature', 'fixtureManifest', 'installer',
	'killSwitchObserverReceipt', 'killSwitchPolicy', 'killSwitchPolicySignature', 'killSwitchRuntimeTrace',
	'killSwitchRuntimeTraceSignature',
	'listeningQualification', 'measuredEvidence', 'modelManifest',
	'modelManifestSignature', 'msiPayloadVerification', 'policy', 'policySignature', 'qualification',
	'qualificationSignature', 'recipeManifest', 'recipeManifestSignature', 'releaseSmoke', 'releaseSmokeSignature',
	'signingResults', 'testGates', 'unsignedModelManifest', 'unsignedRecipeManifest', 'updatePackage',
	'updaterProtocolEvidence', 'updaterProtocolEvidenceSignature', 'updaterVmEvidence', 'updaterVmEvidenceSignature',
	'updaterVmReceipt'
)
$assets = $rehearsal.artifacts
Assert-ExactProperties $assets $requiredAssetNames 'Release rehearsal artifacts'
$resolvedAssets = @{}
foreach ($name in $requiredAssetNames) {
	$record = $assets.$name
	Assert-ExactProperties $record @('fileName', 'sha256', 'size') "Release rehearsal artifact '$name'"
	$fileName = Assert-SafeRelativeReleasePath -Path ([string]$record.fileName) -Context "Release rehearsal artifact '$name'"
	if ($fileName.Contains('/')) { throw "Release rehearsal artifact '$name' must be a direct child of the draft root." }
	$path = Join-Path $rootPath $fileName
	$item = Get-Item -LiteralPath $path -ErrorAction Stop
	if ($item.PSIsContainer -or [int64]$record.size -ne [int64]$item.Length -or
		(Assert-Sha256 $record.sha256 "Release rehearsal artifact '$name'") -cne (Get-ReleaseFileSha256 -Path $item.FullName)) {
		throw "Release rehearsal artifact '$name' does not match its exact staged bytes."
	}
	$resolvedAssets[$name] = $item.FullName
}
if ((Get-ReleaseFileSha256 -Path $resolvedAssets.listeningQualification) -cne $ExpectedListeningQualificationSha256 -or
	(Get-ReleaseFileSha256 -Path $resolvedAssets.modelManifest) -cne [string]$binding.modelManifestSha256 -or
	(Get-ReleaseFileSha256 -Path $resolvedAssets.recipeManifest) -cne [string]$binding.recipeManifestSha256) {
	throw 'Release rehearsal candidate/listening/manifest bytes do not match the protected binding.'
}
if ((Get-ReleaseFileSha256 -Path $resolvedAssets.killSwitchRuntimeTrace) -cne
	[string]$killSwitch.runtimeTraceSha256) {
	throw 'Kill-switch summary is not bound to the exact runtime trace.'
}

$measured = Read-ReleaseJson -Path $resolvedAssets.measuredEvidence
if ([int]$measured.schemaVersion -ne 2 -or [string]$measured.suite -cne 'core_release' -or
	$measured.passed -ne $true -or [string]$measured.sourceSha -cne $ExpectedSourceSha -or
	@($measured.runners).Count -ne 2 -or @($measured.nightlyRunners).Count -ne 2) {
	throw 'Release rehearsal lacks passing schema-v2 master and nightly evidence from both runner classes.'
}
$protectedIdentity = $measured.protectedBuildIdentity
foreach ($pair in @(
	@('tested_binary_sha256', 'testedBinarySha256'), @('staged_payload_sha256', 'stagedPayloadSha256'),
	@('server_binary_sha256', 'serverExecutableSha256'), @('corpus_inventory_sha256', 'corpusInventorySha256'),
	@('model_manifest_sha256', 'modelManifestSha256'), @('recipe_manifest_sha256', 'recipeManifestSha256'),
	@('recipe_set_version', 'recipeSetVersion')
)) {
	if ([string]$protectedIdentity.($pair[0]) -cne [string]$binding.($pair[1])) {
		throw "Release rehearsal measured identity '$($pair[0])' differs from the rehearsed candidate."
	}
}

$listening = Read-ReleaseJson -Path $resolvedAssets.listeningQualification
$listeningBinding = Assert-ObjectProperty $listening 'qualification_binding' 'Listening qualification'
$listeningSuite = [string](Assert-ObjectProperty $listeningBinding 'qualification_suite' 'Listening qualification binding')
$listeningRunnerClass = [string](Assert-ObjectProperty $listeningBinding 'runner_class' 'Listening qualification binding')
$runnerSet = if ($listeningSuite -ceq 'master_quality') { @($measured.runners) } `
	elseif ($listeningSuite -ceq 'nightly') { @($measured.nightlyRunners) } else { @() }
$matchingRunners = @($runnerSet | Where-Object {
	[string]$_.runnerClass -ceq $listeningRunnerClass -and [string]$_.suite -ceq $listeningSuite
})
if ($matchingRunners.Count -ne 1) {
	throw 'Listening qualification does not select exactly one protected measured runner record.'
}
$listeningRunner = $matchingRunners[0]
$listeningInputIdentity = if ($listeningSuite -ceq 'master_quality') {
	$measured.masterInputIdentity
} else {
	$measured.nightlyInputIdentity
}
& (Join-Path $PSScriptRoot 'assert-input-enhancement-listening-qualification.ps1') `
	-ListeningQualificationPath $resolvedAssets.listeningQualification `
	-ExpectedSourceSha $ExpectedSourceSha `
	-ExpectedTestedBinarySha256 ([string]$binding.testedBinarySha256) `
	-ExpectedStagedPayloadSha256 ([string]$binding.stagedPayloadSha256) `
	-ExpectedServerBinarySha256 ([string]$binding.serverExecutableSha256) `
	-ExpectedCorpusInventorySha256 ([string]$binding.corpusInventorySha256) `
	-ExpectedCorpusLockSha256 ([string]$measured.corpusLockSha256) `
	-ExpectedMixturePlanSha256 ([string]$listeningInputIdentity.mixturePlanSha256) `
	-ExpectedCaseSetSha256 ([string]$listeningInputIdentity.caseSetSha256) `
	-ExpectedHarnessSha256 ([string]$measured.harnessSha256) `
	-ExpectedMetricsRuntimeSha256 ([string]$measured.protectedBuildIdentity.metrics_runtime_sha256) `
	-ExpectedModelManifestSha256 ([string]$binding.modelManifestSha256) `
	-ExpectedRecipeManifestSha256 ([string]$binding.recipeManifestSha256) `
	-ExpectedRecipeSetVersion ([string]$binding.recipeSetVersion) `
	-ExpectedReleaseFixturesSha256 ([string]$measured.protectedBuildIdentity.release_fixtures_sha256) `
	-ExpectedRunnerClass $listeningRunnerClass `
	-ExpectedHardwareFingerprintSha256 ([string]$listeningRunner.hardwareFingerprintSha256) `
	-ExpectedQualificationSuite $listeningSuite `
	-ExpectedProtectedQualityQualificationSha256 ([string]$listeningRunner.qualityQualification.sha256) `
	-ExpectedCommunitySize $ExpectedCommunitySize

foreach ($signaturePair in @(
	@('qualification', 'qualificationSignature'), @('releaseSmoke', 'releaseSmokeSignature'),
	@('policy', 'policySignature'), @('killSwitchPolicy', 'killSwitchPolicySignature'),
	@('killSwitchRuntimeTrace', 'killSwitchRuntimeTraceSignature'),
	@('channelPointer', 'channelPointerSignature'),
	@('updaterProtocolEvidence', 'updaterProtocolEvidenceSignature'),
	@('updaterVmEvidence', 'updaterVmEvidenceSignature')
)) {
	& (Join-Path $PSScriptRoot 'assert-input-enhancement-detached-signature.ps1') `
		-InputPath $resolvedAssets[$signaturePair[0]] `
		-SignaturePath $resolvedAssets[$signaturePair[1]] `
		-PublicKeyHex $ed25519PublicKey -OpenSslPath $OpenSslPath
}

$qualification = Read-ReleaseJson -Path $resolvedAssets.qualification
if ([string]$qualification.buildId -cne $ExpectedBuildId -or [string]$qualification.source.sha -cne $ExpectedSourceSha) {
	throw 'Release rehearsal qualification belongs to another build.'
}
& (Join-Path $PSScriptRoot 'assert-input-enhancement-qualification.ps1') `
	-QualificationPath $resolvedAssets.qualification `
	-ArtifactPath $resolvedAssets.auditArtifact `
	-InstallerPath $resolvedAssets.installer `
	-MsiPayloadVerificationPath $resolvedAssets.msiPayloadVerification `
	-UpdatePackagePath $resolvedAssets.updatePackage `
	-ModelManifestPath $resolvedAssets.modelManifest `
	-RecipeManifestPath $resolvedAssets.recipeManifest `
	-UnsignedModelManifestPath $resolvedAssets.unsignedModelManifest `
	-UnsignedRecipeManifestPath $resolvedAssets.unsignedRecipeManifest `
	-ModelManifestSignaturePath $resolvedAssets.modelManifestSignature `
	-RecipeManifestSignaturePath $resolvedAssets.recipeManifestSignature `
	-ExpectedEd25519PublicKeyHex $ed25519PublicKey `
	-TestGateResultsPath $resolvedAssets.testGates `
	-SigningResultsPath $resolvedAssets.signingResults `
	-MeasuredEvidencePath $resolvedAssets.measuredEvidence `
	-ExpectedSourceSha $ExpectedSourceSha `
	-ExpectedBuildId $ExpectedBuildId `
	-ExpectedSignerSubject ([string]$signing.certificateSubject) `
	-OpenSslPath $OpenSslPath -PythonPath $PythonPath

& (Join-Path $PSScriptRoot 'assert-input-enhancement-updater-protocol-evidence.ps1') `
	-EvidencePath $resolvedAssets.updaterProtocolEvidence `
	-ExpectedSourceSha $ExpectedSourceSha -ExpectedBuildId $ExpectedBuildId `
	-ExpectedCandidatePayloadSha256 ([string]$binding.stagedPayloadSha256)
& (Join-Path $PSScriptRoot 'assert-input-enhancement-updater-vm-evidence.ps1') `
	-EvidencePath $resolvedAssets.updaterVmEvidence `
	-ExpectedSourceSha $ExpectedSourceSha -ExpectedBuildId $ExpectedBuildId `
	-ExpectedCandidatePayloadSha256 ([string]$binding.stagedPayloadSha256) `
	-ExpectedCandidateInstallerSha256 ([string]$qualification.installer.sha256) `
	-ExpectedCandidateExecutableSha256 ([string]$qualification.installer.executableSha256) `
	-ExpectedHarnessSha256 $ExpectedExecutorSha256 `
	-ReceiptPath $resolvedAssets.updaterVmReceipt `
	-ExpectedReceiptSha256 $ExpectedUpdaterVmReceiptSha256 `
	-ExpectedVmExecutorSha256 $ExpectedUpdaterVmExecutorSha256 `
	-ExpectedImageSha256 $ExpectedUpdaterVmImageSha256 `
	-ExpectedSnapshotSha256 $ExpectedUpdaterVmSnapshotSha256 `
	-ExpectedHardwareFingerprintSha256 $ExpectedUpdaterVmHardwareFingerprintSha256

$expandedRoot = Join-Path ([IO.Path]::GetTempPath()) ('mumble-rehearsal-expanded-' + [guid]::NewGuid().ToString('N'))
try {
	& (Join-Path $PSScriptRoot 'assert-windows-update-package.ps1') `
		-PackagePath $resolvedAssets.updatePackage `
		-ExpectedCommit $ExpectedSourceSha `
		-ExpectedBuild ([int]$qualification.buildNumber) `
		-ExpectedVersion "1.7.$($qualification.buildNumber)" `
		-RequireUpdaterRuntime -RequireGStreamerRuntime -ExpandedPayloadPath $expandedRoot
	& (Join-Path $PSScriptRoot 'assert-input-enhancement-release-smoke.ps1') `
		-ReleaseSmokePath $resolvedAssets.releaseSmoke `
		-QualificationPath $resolvedAssets.qualification `
		-ModelManifestPath $resolvedAssets.modelManifest `
		-RecipeManifestPath $resolvedAssets.recipeManifest `
		-ModelManifestSignaturePath $resolvedAssets.modelManifestSignature `
		-RecipeManifestSignaturePath $resolvedAssets.recipeManifestSignature `
		-ArtifactPath $resolvedAssets.updatePackage `
		-ExpandedArtifactRoot $expandedRoot `
		-ExpectedSourceSha $ExpectedSourceSha `
		-ExpectedBuildId $ExpectedBuildId `
		-ExpectedHarnessSha256 $ExpectedReleaseSmokeHarnessSha256 `
		-FixtureManifestPath $resolvedAssets.fixtureManifest `
		-CaseSetPath $resolvedAssets.caseSet `
		-ExpectedFixtureManifestSha256 $ExpectedFixtureManifestSha256 `
		-ExpectedCaseSetSha256 $ExpectedCaseSetSha256 `
		-ExpectedServerExecutableSha256 $ExpectedServerExecutableSha256
} finally {
	Remove-Item -LiteralPath $expandedRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$normalPolicy = Assert-CanonicalInputEnhancementPolicy -Path $resolvedAssets.policy `
	-ExpectedMinBuild ([uint64]$qualification.buildNumber) -ExpectedRecipeSetVersion ([string]$binding.recipeSetVersion) `
	-RequireCurrentlyValid
$emergencyPolicy = Assert-CanonicalInputEnhancementPolicy -Path $resolvedAssets.killSwitchPolicy `
	-ExpectedMinBuild ([uint64]$qualification.buildNumber) -ExpectedRecipeSetVersion ([string]$binding.recipeSetVersion) `
	-RequireCurrentlyValid
if ($normalPolicy.forceOriginal -eq $true -or $emergencyPolicy.forceOriginal -ne $true -or
	[string]$emergencyPolicy.recommendedProfile -cne 'Original') {
	throw 'Release rehearsal did not exercise a separate signed policy-only force-Original kill switch.'
}
$trace = Read-ReleaseJson -Path $resolvedAssets.killSwitchRuntimeTrace
& (Join-Path $PSScriptRoot 'assert-input-enhancement-kill-switch-observation.ps1') `
	-RuntimeTracePath $resolvedAssets.killSwitchRuntimeTrace `
	-ReceiptPath $resolvedAssets.killSwitchObserverReceipt `
	-ExpectedReceiptSha256 $ExpectedKillSwitchObserverReceiptSha256 `
	-ExpectedSourceSha $ExpectedSourceSha -ExpectedBuildId $ExpectedBuildId `
	-ExpectedObserverSha256 $ExpectedKillSwitchObserverSha256 `
	-ExpectedTestedBinarySha256 ([string]$binding.testedBinarySha256) `
	-ExpectedStagedPayloadSha256 ([string]$binding.stagedPayloadSha256) `
	-ExpectedPolicySha256 (Get-ReleaseFileSha256 -Path $resolvedAssets.killSwitchPolicy)
$traceStarted = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$trace.startedAtUtc,
	[Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind,
	[ref]$traceStarted)) { throw 'Kill-switch runtime trace timestamp is invalid.' }
$expectedTraceEvents = @('startup-policy-check', 'refresh-scheduled', 'force-original-policy-verified', 'profile-transitioned')
$traceEvents = @($trace.events)
if ($traceEvents.Count -ne $expectedTraceEvents.Count) { throw 'Kill-switch runtime trace is incomplete.' }
$previousElapsed = -1
foreach ($index in 0..($traceEvents.Count - 1)) {
	$event = $traceEvents[$index]
	Assert-ExactProperties $event @('activeProfile', 'elapsedMilliseconds', 'event', 'policySha256', 'sequence') `
		"Kill-switch runtime trace event[$index]"
	$elapsed = [int64]$event.elapsedMilliseconds
	if ([int]$event.sequence -ne $index -or [string]$event.event -cne $expectedTraceEvents[$index] -or
		$elapsed -lt $previousElapsed -or $elapsed -lt 0 -or $elapsed -gt 1200000 -or
		[string]$event.policySha256 -cne [string]$trace.policySha256) {
		throw "Kill-switch runtime trace event $index is invalid."
	}
	$previousElapsed = $elapsed
}
$transitionEvent = $traceEvents[-1]
if ([string]$transitionEvent.activeProfile -cne 'Original' -or
	[math]::Abs(([double]$transitionEvent.elapsedMilliseconds / 1000.0) -
		[double]$killSwitch.observedForceOriginalSeconds) -gt 0.001) {
	throw 'Kill-switch runtime trace does not prove the reported transition to Original.'
}
$pointer = Read-ReleaseJson -Path $resolvedAssets.channelPointer
if ([int]$pointer.schemaVersion -ne 2 -or [string]$pointer.immutableTag -cne $ExpectedBuildId -or
	@($pointer.recoveryInstallers).Count -ne 2 -or @($pointer.knownGoodTags).Count -ne 3) {
	throw 'Release rehearsal channel pointer is not the strict v2 two-recovery contract.'
}

Write-Host "Pre-Azure release rehearsal passed for '$ExpectedBuildId' with master/nightly, listening, 6+24 smoke, rollback metadata, and kill-switch evidence."
