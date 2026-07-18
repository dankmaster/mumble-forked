[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$ListeningQualificationPath,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{40}$')]
	[string]$ExpectedSourceSha,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedTestedBinarySha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedStagedPayloadSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedServerBinarySha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedCorpusInventorySha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedCorpusLockSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedMixturePlanSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedCaseSetSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedHarnessSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedMetricsRuntimeSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedModelManifestSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedRecipeManifestSha256,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')]
	[string]$ExpectedRecipeSetVersion,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedReleaseFixturesSha256,

	[Parameter(Mandatory = $true)]
	[ValidateSet('low-performance', 'mainstream')]
	[string]$ExpectedRunnerClass,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedHardwareFingerprintSha256,

	[Parameter(Mandatory = $true)]
	[ValidateSet('master_quality', 'nightly')]
	[string]$ExpectedQualificationSuite,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedProtectedQualityQualificationSha256,

	[ValidateRange(0, 2)]
	[int]$ExpectedCommunitySize = 0,

	[string]$PythonExecutable = ''
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

function Get-FiniteNumber {
	param([object]$Value, [string]$Context)
	$number = [double]$Value
	if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) {
		throw "$Context must be finite."
	}
	return $number
}

function Get-WholeNumber {
	param([object]$Value, [string]$Context)
	$number = Get-FiniteNumber $Value $Context
	if ($number -lt 0 -or [math]::Truncate($number) -ne $number) {
		throw "$Context must be a non-negative whole number."
	}
	return [int64]$number
}

$evidence = Read-ReleaseJson -Path $ListeningQualificationPath
Assert-ExactProperties $evidence @(
	'answer_key_sha256', 'listener_count', 'minimum_clean_pairs_per_session', 'minimum_pairs_per_session',
	'minimum_quality_noisy_decisive_votes_per_session', 'minimum_quality_noisy_pairs_per_session',
	'minimum_voice_focus_severe_decisive_votes_per_session', 'minimum_voice_focus_severe_pairs_per_session',
	'pack_id', 'quality_intelligibility_median', 'quality_noisy_preference', 'quality_noisy_vote_evidence',
	'qualification_binding', 'recurring_clean_artifacts', 'schema_version', 'session_count',
	'session_manifest', 'source_manifest_sha256', 'status', 'voice_focus_intelligibility_median',
	'voice_focus_severe_preference', 'voice_focus_severe_vote_evidence'
) 'Listening qualification'
if ([int]$evidence.schema_version -ne 3 -or [string]$evidence.status -cne 'passed') {
	throw 'Listening qualification must be a passing schema-v3 aggregate.'
}

$resolvedPython = $null
if (-not [string]::IsNullOrWhiteSpace($PythonExecutable)) {
	if (Test-Path -LiteralPath $PythonExecutable -PathType Leaf) {
		$resolvedPython = (Resolve-Path -LiteralPath $PythonExecutable).Path
	} else {
		$command = Get-Command $PythonExecutable -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($command) { $resolvedPython = $command.Source }
	}
} else {
	foreach ($candidate in @('python', 'python3')) {
		$command = Get-Command $candidate -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($command) { $resolvedPython = $command.Source; break }
	}
}
if (-not $resolvedPython) {
	throw 'Listening qualification semantic verification requires a Python 3 executable.'
}
$semanticValidator = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\audio-quality\blind-listening.py')).Path
$semanticArguments = @($semanticValidator, '--validate-qualification', (Resolve-Path -LiteralPath $ListeningQualificationPath).Path)
if ($ExpectedCommunitySize -gt 0) {
	$semanticArguments += @('--expected-community-size', [string]$ExpectedCommunitySize)
}
$semanticOutput = & $resolvedPython @semanticArguments 2>&1
if ($LASTEXITCODE -ne 0) {
	throw "Listening qualification session-evidence recomputation failed: $($semanticOutput -join [Environment]::NewLine)"
}
foreach ($name in @('answer_key_sha256', 'pack_id', 'source_manifest_sha256')) {
	if ([string](Assert-ObjectProperty $evidence $name 'Listening qualification') -cnotmatch '^[0-9a-f]{64}$') {
		throw "Listening qualification '$name' is invalid."
	}
}
$listenerCount = Get-WholeNumber $evidence.listener_count 'Listening listener_count'
$sessionCount = Get-WholeNumber $evidence.session_count 'Listening session_count'
if ($listenerCount -lt 1 -or $sessionCount -lt $listenerCount) {
	throw 'Listening qualification has an invalid listener/session count.'
}
if ($ExpectedCommunitySize -eq 0) {
	if ($listenerCount -lt 3) { throw 'Listening qualification requires at least three distinct listeners.' }
} elseif ($listenerCount -ne $ExpectedCommunitySize -or $sessionCount -lt (2 * $ExpectedCommunitySize)) {
	throw 'Small-community listening qualification requires every declared listener and two sessions per listener.'
}
$minimumPairs = Get-WholeNumber $evidence.minimum_pairs_per_session 'Listening minimum_pairs_per_session'
$minimumCleanPairs = Get-WholeNumber $evidence.minimum_clean_pairs_per_session 'Listening minimum_clean_pairs_per_session'
$minimumQualityPairs = Get-WholeNumber $evidence.minimum_quality_noisy_pairs_per_session 'Listening minimum_quality_noisy_pairs_per_session'
$minimumVoicePairs = Get-WholeNumber $evidence.minimum_voice_focus_severe_pairs_per_session 'Listening minimum_voice_focus_severe_pairs_per_session'
$minimumQualityDecisive = Get-WholeNumber $evidence.minimum_quality_noisy_decisive_votes_per_session 'Listening minimum_quality_noisy_decisive_votes_per_session'
$minimumVoiceDecisive = Get-WholeNumber $evidence.minimum_voice_focus_severe_decisive_votes_per_session 'Listening minimum_voice_focus_severe_decisive_votes_per_session'
if ($minimumPairs -lt 26 -or $minimumCleanPairs -lt 2 -or $minimumQualityPairs -lt 12 -or
	$minimumVoicePairs -lt 12 -or $minimumQualityDecisive -lt 8 -or $minimumVoiceDecisive -lt 8) {
	throw 'Listening qualification lacks the required per-session relevant/randomized/decisive pair coverage.'
}
$qualityPreference = Get-FiniteNumber $evidence.quality_noisy_preference 'Quality preference'
$voicePreference = Get-FiniteNumber $evidence.voice_focus_severe_preference 'Voice Focus preference'
$voiceIntelligibility = Get-FiniteNumber $evidence.voice_focus_intelligibility_median 'Voice Focus intelligibility'
$qualityIntelligibility = Get-FiniteNumber $evidence.quality_intelligibility_median 'Quality intelligibility'
if ($qualityPreference -lt 0.60 -or $qualityPreference -gt 1.0) {
	throw 'Quality must be preferred over Original in at least 60% of noisy non-tie votes.'
}
if ($voicePreference -lt 0.60 -or $voicePreference -gt 1.0 -or $voiceIntelligibility -lt $qualityIntelligibility) {
	throw 'Voice Focus must win at least 60% of severe-noise votes without worse median intelligibility.'
}
if (@($evidence.recurring_clean_artifacts).Count -ne 0) {
	throw 'Listening qualification contains a recurring clean-speech artifact.'
}

foreach ($voteName in @('quality_noisy_vote_evidence', 'voice_focus_severe_vote_evidence')) {
	$votes = Assert-ObjectProperty $evidence $voteName 'Listening qualification'
	Assert-ExactProperties $votes @('comparator', 'decisive', 'preferred', 'presented', 'ties') "Listening $voteName"
	foreach ($field in @('comparator', 'decisive', 'preferred', 'presented', 'ties')) {
		$null = Get-WholeNumber (Assert-ObjectProperty $votes $field "Listening $voteName") "Listening $voteName.$field"
	}
	if ([int64]$votes.presented -ne ([int64]$votes.decisive + [int64]$votes.ties) -or
		[int64]$votes.decisive -ne ([int64]$votes.preferred + [int64]$votes.comparator) -or
		[int64]$votes.presented -lt (12 * $sessionCount) -or
		[int64]$votes.decisive -lt (8 * $sessionCount)) {
		throw "Listening $voteName does not contain enough internally consistent non-tie evidence."
	}
}
$calculatedQualityPreference = [double]$evidence.quality_noisy_vote_evidence.preferred / [double]$evidence.quality_noisy_vote_evidence.decisive
$calculatedVoicePreference = [double]$evidence.voice_focus_severe_vote_evidence.preferred / [double]$evidence.voice_focus_severe_vote_evidence.decisive
if ([math]::Abs($calculatedQualityPreference - $qualityPreference) -gt 0.000000001 -or
	[math]::Abs($calculatedVoicePreference - $voicePreference) -gt 0.000000001) {
	throw 'Listening preference rates do not match their decisive vote counts.'
}

$binding = Assert-ObjectProperty $evidence 'qualification_binding' 'Listening qualification'
Assert-ExactProperties $binding @(
	'case_set_sha256', 'corpus_inventory_sha256', 'corpus_lock_sha256', 'git_sha',
	'hardware_fingerprint_sha256', 'harness_sha256', 'metrics_runtime_sha256', 'mixture_plan_sha256',
	'model_manifest_sha256', 'protected_quality_qualification_sha256', 'qualification_suite',
	'recipe_manifest_sha256', 'recipe_set_version', 'release_fixtures_sha256', 'runner_class',
	'server_binary_sha256', 'staged_payload_sha256', 'tested_binary_sha256'
) 'Listening qualification binding'
$expected = [ordered]@{
	git_sha                  = $ExpectedSourceSha
	tested_binary_sha256     = $ExpectedTestedBinarySha256
	staged_payload_sha256    = $ExpectedStagedPayloadSha256
	server_binary_sha256     = $ExpectedServerBinarySha256
	corpus_lock_sha256       = $ExpectedCorpusLockSha256
	corpus_inventory_sha256  = $ExpectedCorpusInventorySha256
	mixture_plan_sha256      = $ExpectedMixturePlanSha256
	case_set_sha256          = $ExpectedCaseSetSha256
	harness_sha256           = $ExpectedHarnessSha256
	metrics_runtime_sha256   = $ExpectedMetricsRuntimeSha256
	model_manifest_sha256    = $ExpectedModelManifestSha256
	recipe_manifest_sha256   = $ExpectedRecipeManifestSha256
	recipe_set_version       = $ExpectedRecipeSetVersion
	release_fixtures_sha256  = $ExpectedReleaseFixturesSha256
	runner_class             = $ExpectedRunnerClass
	hardware_fingerprint_sha256 = $ExpectedHardwareFingerprintSha256
	qualification_suite      = $ExpectedQualificationSuite
	protected_quality_qualification_sha256 = $ExpectedProtectedQualityQualificationSha256
}
foreach ($name in $expected.Keys) {
	if ([string](Assert-ObjectProperty $binding $name 'Listening qualification binding') -cne [string]$expected[$name]) {
		throw "Listening qualification binding '$name' does not match the qualified candidate."
	}
}

Write-Host "Listening qualification passed after recomputing $sessionCount canonical session file(s) for $listenerCount listener(s) and the exact qualified payload."
