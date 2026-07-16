[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$EvidencePath,

	[Parameter(Mandatory = $true)]
	[string]$SignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$PublicKeyHex,

	[Parameter(Mandatory = $true)]
	[string]$AggregateExportPath,

	[Parameter(Mandatory = $true)]
	[string]$AggregateExportSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$AggregatePublicKeyHex,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedQuerySha256,

	[Parameter(Mandatory = $true)]
	[ValidateSet("community-stable", "stable-opt-in", "auto-recommended", "auto-default")]
	[string]$TargetStage,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedBuildId,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedRecipeSetVersion,

	[int]$MaximumEvidenceAgeDays = 7,

	[string]$RnnoiseDecisionPath = "",

	[string]$RnnoiseDecisionSignaturePath = "",

	[string]$PythonPath = "python",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

function Assert-ExactProperties {
	param([object]$Value, [string[]]$Names, [string]$Context)
	if ($null -eq $Value) { throw "$Context must be an object." }
	$actual = @($Value.PSObject.Properties.Name | Sort-Object)
	$expected = @($Names | Sort-Object)
	if (@(Compare-Object $expected $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties. Expected: $($expected -join ', ')."
	}
}

function Get-WholeNumber {
	param([object]$Value, [string]$Context, [double]$Minimum = 0)
	[double]$number = 0
	if ($Value -is [bool] -or -not [double]::TryParse(
		[Convert]::ToString($Value, [Globalization.CultureInfo]::InvariantCulture),
		[Globalization.NumberStyles]::Float,
		[Globalization.CultureInfo]::InvariantCulture,
		[ref]$number) -or [double]::IsNaN($number) -or [double]::IsInfinity($number) -or
		$number -lt $Minimum -or [Math]::Floor($number) -ne $number) {
		throw "$Context must be a whole number greater than or equal to $Minimum."
	}
	return [int64]$number
}

function Get-FiniteNumber {
	param([object]$Value, [string]$Context, [double]$Minimum = 0, [double]$Maximum = [double]::MaxValue)
	[double]$number = 0
	if ($Value -is [bool] -or -not [double]::TryParse(
		[Convert]::ToString($Value, [Globalization.CultureInfo]::InvariantCulture),
		[Globalization.NumberStyles]::Float,
		[Globalization.CultureInfo]::InvariantCulture,
		[ref]$number) -or [double]::IsNaN($number) -or [double]::IsInfinity($number) -or
		$number -lt $Minimum -or $number -gt $Maximum) {
		throw "$Context must be a finite number between $Minimum and $Maximum."
	}
	return $number
}

function Get-UtcTimestamp {
	param([object]$Value, [string]$Context)
	if ($Value -is [DateTimeOffset]) {
		return ([DateTimeOffset]$Value).ToUniversalTime()
	}
	if ($Value -is [DateTime]) {
		return [DateTimeOffset]::new(([DateTime]$Value).ToUniversalTime())
	}
	$text = [string]$Value
	[DateTimeOffset]$timestamp = [DateTimeOffset]::MinValue
	if (-not [DateTimeOffset]::TryParseExact(
		$text,
		"yyyy-MM-dd'T'HH:mm:ss'Z'",
		[Globalization.CultureInfo]::InvariantCulture,
		[Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal,
		[ref]$timestamp)) {
		throw "$Context must use canonical UTC seconds (yyyy-MM-ddTHH:mm:ssZ)."
	}
	return $timestamp
}

if ($MaximumEvidenceAgeDays -lt 1 -or $MaximumEvidenceAgeDays -gt 30) {
	throw "MaximumEvidenceAgeDays must be between 1 and 30."
}
if ($ExpectedBuildId -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$') {
	throw "ExpectedBuildId is not an immutable input-enhancement build ID."
}
if ($ExpectedRecipeSetVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
	throw "ExpectedRecipeSetVersion is invalid."
}

$rolloutKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
$aggregateKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $AggregatePublicKeyHex
if ($rolloutKey -ceq $aggregateKey) {
	throw "Telemetry aggregate exports and rollout envelopes must use separate Ed25519 signer identities."
}
if (-not (Test-Ed25519DetachedSignature -InputPath $EvidencePath -SignaturePath $SignaturePath `
	-PublicKeyHex $rolloutKey -OpenSslPath $OpenSslPath)) {
	throw "Rollout qualification has no valid detached Ed25519 signature."
}
$evidenceFile = Get-Item -LiteralPath $EvidencePath -ErrorAction Stop
$signatureFile = Get-Item -LiteralPath $SignaturePath -ErrorAction Stop
if ($evidenceFile.Name -cne 'input-enhancement-rollout.json' -or
	$signatureFile.Name -cne 'input-enhancement-rollout.json.sig') {
	throw "Rollout qualification files must use the stable input-enhancement-rollout.json[.sig] names."
}
if ($evidenceFile.Length -le 0 -or $evidenceFile.Length -gt 32768 -or $signatureFile.Length -ne 64) {
	throw "Rollout qualification or signature has an unsafe size."
}

Assert-StrictInputEnhancementRolloutJson -Path $evidenceFile.FullName -Kind rollout -PythonPath $PythonPath

$evidence = Read-ReleaseJson -Path $evidenceFile.FullName
Assert-ExactProperties $evidence @(
	'domainRnnoiseTrack', 'generatedAtUtc', 'kind', 'schemaVersion', 'sourceAggregate'
) 'Rollout qualification'
if ([int](Assert-ObjectProperty $evidence 'schemaVersion' 'Rollout qualification') -ne 2 -or
	[string](Assert-ObjectProperty $evidence 'kind' 'Rollout qualification') -cne
		'input-enhancement-rollout-qualification') {
	throw "Unsupported rollout qualification schema."
}

$generatedAt = Get-UtcTimestamp (Assert-ObjectProperty $evidence 'generatedAtUtc' 'Rollout qualification') `
	'rollout.generatedAtUtc'
$now = [DateTimeOffset]::UtcNow
if ($generatedAt -gt $now.AddMinutes(5) -or $generatedAt -lt $now.AddDays(-$MaximumEvidenceAgeDays)) {
	throw "Rollout qualification is future-dated or older than the allowed evidence age."
}

& (Join-Path $PSScriptRoot 'assert-input-enhancement-aggregate-export.ps1') `
	-AggregateExportPath $AggregateExportPath `
	-AggregateExportSignaturePath $AggregateExportSignaturePath `
	-AggregatePublicKeyHex $aggregateKey `
	-ExpectedQuerySha256 $ExpectedQuerySha256 `
	-MaximumEvidenceAgeDays $MaximumEvidenceAgeDays `
	-PythonPath $PythonPath `
	-OpenSslPath $OpenSslPath

$aggregateFile = Get-Item -LiteralPath $AggregateExportPath -ErrorAction Stop
$aggregateSignatureFile = Get-Item -LiteralPath $AggregateExportSignaturePath -ErrorAction Stop
$aggregate = Read-ReleaseJson -Path $aggregateFile.FullName
$aggregateQuery = Assert-ObjectProperty $aggregate 'query' 'Telemetry aggregate export'
$sourceAggregate = Assert-ObjectProperty $evidence 'sourceAggregate' 'Rollout qualification'
Assert-ExactProperties $sourceAggregate @(
	'fileName', 'querySha256', 'sha256', 'signatureFileName', 'signatureSha256', 'windowSha256'
) 'Rollout source aggregate'
if ([string]$sourceAggregate.fileName -cne $aggregateFile.Name -or
	[string]$sourceAggregate.signatureFileName -cne $aggregateSignatureFile.Name -or
	[string]$sourceAggregate.sha256 -cne (Get-ReleaseFileSha256 -Path $aggregateFile.FullName) -or
	[string]$sourceAggregate.signatureSha256 -cne (Get-ReleaseFileSha256 -Path $aggregateSignatureFile.FullName) -or
	[string]$sourceAggregate.querySha256 -cne [string]$aggregateQuery.sha256 -or
	[string]$sourceAggregate.windowSha256 -cne [string]$aggregateQuery.windowSha256) {
	throw "Rollout qualification does not reference the exact verified aggregate export bytes and query/window binding."
}
$aggregateGeneratedAt = Get-UtcTimestamp (Assert-ObjectProperty $aggregate 'generatedAtUtc' 'Telemetry aggregate export') `
	'aggregate.generatedAtUtc'
if ($generatedAt -lt $aggregateGeneratedAt) {
	throw "Rollout qualification predates its telemetry aggregate export."
}

$window = Assert-ObjectProperty $aggregate 'window' 'Telemetry aggregate export'
$windowStart = Get-UtcTimestamp (Assert-ObjectProperty $window 'startUtc' 'Aggregate window') 'aggregate.window.startUtc'
$windowEnd = Get-UtcTimestamp (Assert-ObjectProperty $window 'endUtc' 'Aggregate window') 'aggregate.window.endUtc'
$observationDays = Get-WholeNumber (Assert-ObjectProperty $window 'observationDays' 'Aggregate window') `
	'aggregate.window.observationDays' 1
if ($windowStart -ge $windowEnd -or $windowEnd -gt $aggregateGeneratedAt -or
	($windowEnd - $windowStart).TotalDays -lt $observationDays) {
	throw "Rollout aggregate window is inconsistent with its attested observation days."
}

$sourceChannel = [string](Assert-ObjectProperty $aggregate 'sourceChannel' 'Telemetry aggregate export')
$rolloutAudience = [string](Assert-ObjectProperty $aggregate 'rolloutAudience' 'Telemetry aggregate export')
$requiredSourceChannel = if ($TargetStage -cin @('community-stable', 'stable-opt-in')) { 'preview' } else { 'stable' }
$requiredAudience = if ($TargetStage -ceq 'community-stable') { 'private-community' } else { 'public' }
if ($sourceChannel -cne $requiredSourceChannel) {
	throw "$TargetStage requires evidence collected on the '$requiredSourceChannel' channel."
}
if ($rolloutAudience -cne $requiredAudience) {
	throw "$TargetStage requires rollout audience '$requiredAudience'."
}
$testedBuildIds = @((Assert-ObjectProperty $aggregate 'testedBuildIds' 'Telemetry aggregate export') |
	ForEach-Object { [string]$_ })
if ($testedBuildIds.Count -ne 1 -or
	@($testedBuildIds | Where-Object { $_ -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' }).Count -gt 0 -or
	[string]$testedBuildIds[0] -cne $ExpectedBuildId) {
	throw "Rollout aggregate must contain only the exact immutable build being promoted."
}

$population = Assert-ObjectProperty $aggregate 'population' 'Telemetry aggregate export'
$users = Get-WholeNumber (Assert-ObjectProperty $population 'distinctUsers' 'Aggregate population') `
	'aggregate.population.distinctUsers'
$devices = Get-WholeNumber (Assert-ObjectProperty $population 'distinctDevices' 'Aggregate population') `
	'aggregate.population.distinctDevices'
$intendedCommunityDevices = Get-WholeNumber `
	(Assert-ObjectProperty $population 'intendedCommunityDevices' 'Aggregate population') `
	'aggregate.population.intendedCommunityDevices' 1
$talkHours = Get-FiniteNumber (Assert-ObjectProperty $population 'talkHours' 'Aggregate population') `
	'aggregate.population.talkHours'

$reliability = Assert-ObjectProperty $aggregate 'reliability' 'Telemetry aggregate export'
$p0 = Get-WholeNumber (Assert-ObjectProperty $reliability 'p0Count' 'Aggregate reliability') `
	'aggregate.reliability.p0Count'
$p1 = Get-WholeNumber (Assert-ObjectProperty $reliability 'p1Count' 'Aggregate reliability') `
	'aggregate.reliability.p1Count'
$hashFailures = Get-WholeNumber (Assert-ObjectProperty $reliability 'modelHashMismatchCount' 'Aggregate reliability') `
	'aggregate.reliability.modelHashMismatchCount'
$callbackRegressions = Get-WholeNumber `
	(Assert-ObjectProperty $reliability 'recurrentCallbackRegressionCount' 'Aggregate reliability') `
	'aggregate.reliability.recurrentCallbackRegressionCount'
$crashFree = Get-FiniteNumber (Assert-ObjectProperty $reliability 'crashFreeSessionRate' 'Aggregate reliability') `
	'aggregate.reliability.crashFreeSessionRate' 0 1
$fallbackRate = Get-FiniteNumber (Assert-ObjectProperty $reliability 'fallbackSessionRate' 'Aggregate reliability') `
	'aggregate.reliability.fallbackSessionRate' 0 1
$overrunRate = Get-FiniteNumber (Assert-ObjectProperty $reliability 'callbackOverrunFrameRate' 'Aggregate reliability') `
	'aggregate.reliability.callbackOverrunFrameRate' 0 1
$optOutRate = Get-FiniteNumber (Assert-ObjectProperty $reliability 'manualRollbackOrOptOutRate' 'Aggregate reliability') `
	'aggregate.reliability.manualRollbackOrOptOutRate' 0 1
if ($p0 -ne 0 -or $p1 -ne 0 -or $hashFailures -ne 0 -or $callbackRegressions -ne 0) {
	throw "Rollout aggregate contains a P0/P1, model-hash failure, or recurrent callback regression."
}

$preference = Assert-ObjectProperty $aggregate 'preference' 'Telemetry aggregate export'
$blindResponses = Get-WholeNumber (Assert-ObjectProperty $preference 'blindAbResponses' 'Aggregate preference') `
	'aggregate.preference.blindAbResponses'
$preferenceRate = Get-FiniteNumber (Assert-ObjectProperty $preference 'selectedOverOriginalRate' 'Aggregate preference') `
	'aggregate.preference.selectedOverOriginalRate' 0 1

$domainTrack = Assert-ObjectProperty $evidence 'domainRnnoiseTrack' 'Rollout qualification'
$domainStatus = [string](Assert-ObjectProperty $domainTrack 'status' 'Domain RNNoise track')
$hasDecision = -not [string]::IsNullOrWhiteSpace($RnnoiseDecisionPath)
$hasDecisionSignature = -not [string]::IsNullOrWhiteSpace($RnnoiseDecisionSignaturePath)
if ($hasDecision -xor $hasDecisionSignature) {
	throw "RNNoise decision evidence is incomplete."
}
if ($domainStatus -ceq 'pending') {
	Assert-ExactProperties $domainTrack @('status') 'Pending domain RNNoise track'
	if ($hasDecision) {
		throw "A pending RNNoise track must not receive unattested decision files."
	}
} elseif ($domainStatus -ceq 'completed') {
	Assert-ExactProperties $domainTrack @('decision', 'outcome', 'status') 'Completed domain RNNoise track'
	if (-not $hasDecision) {
		throw "A completed RNNoise track requires its exact signed selection decision."
	}
	$decisionEvidence = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rnnoise-selection-decision.ps1') `
		-DecisionPath $RnnoiseDecisionPath `
		-DecisionSignaturePath $RnnoiseDecisionSignaturePath `
		-PublicKeyHex $rolloutKey `
		-PythonPath $PythonPath `
		-OpenSslPath $OpenSslPath
	$decisionReference = Assert-ObjectProperty $domainTrack 'decision' 'Completed domain RNNoise track'
	Assert-ExactProperties $decisionReference @('fileName', 'sha256', 'signatureFileName', 'signatureSha256') `
		'RNNoise decision reference'
	$domainOutcome = [string](Assert-ObjectProperty $domainTrack 'outcome' 'Completed domain RNNoise track')
	if ($domainOutcome -cne [string]$decisionEvidence.rolloutOutcome -or
		[string]$decisionReference.fileName -cne [string]$decisionEvidence.fileName -or
		[string]$decisionReference.sha256 -cne [string]$decisionEvidence.sha256 -or
		[string]$decisionReference.signatureFileName -cne [string]$decisionEvidence.signatureFileName -or
		[string]$decisionReference.signatureSha256 -cne [string]$decisionEvidence.signatureSha256) {
		throw "Completed RNNoise track does not bind the exact verified campaign selection decision."
	}
} else {
	throw "Domain RNNoise track has an unsupported status."
}

$privacy = Assert-ObjectProperty $aggregate 'privacy' 'Telemetry aggregate export'
if ((Assert-ObjectProperty $privacy 'optInOnly' 'Aggregate privacy') -ne $true -or
	(Assert-ObjectProperty $privacy 'rawAudioIncluded' 'Aggregate privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'rawDeviceIdsIncluded' 'Aggregate privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'transcriptsIncluded' 'Aggregate privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'voiceprintsIncluded' 'Aggregate privacy') -ne $false -or
	(Get-WholeNumber (Assert-ObjectProperty $privacy 'retentionDays' 'Aggregate privacy') `
		'aggregate.privacy.retentionDays' 1) -gt 30) {
	throw "Rollout aggregate violates the input-enhancement telemetry privacy contract."
}

switch ($TargetStage) {
	'community-stable' {
		if ($devices -lt $intendedCommunityDevices -or $talkHours -lt 20 -or $observationDays -lt 7) {
			throw "Private community stable requires all intended devices, 20 talk hours, and 7 observation days."
		}
	}
	'stable-opt-in' {
		if ($users -lt 10 -or $devices -lt 10 -or $talkHours -lt 50 -or $observationDays -lt 7) {
			throw "Stable opt-in requires 10 users/devices, 50 talk hours, and 7 observation days."
		}
	}
	'auto-recommended' {
		if ($users -lt 25 -or $devices -lt 25 -or $talkHours -lt 200 -or $observationDays -lt 14 -or
			$domainStatus -cne 'completed') {
			throw "Auto recommendation requires 25 users/devices, 200 talk hours, 14 days, and a completed domain RNNoise track."
		}
	}
	'auto-default' {
		if ($devices -lt 50 -or $talkHours -lt 500 -or $observationDays -lt 30 -or $domainStatus -cne 'completed' -or
			$crashFree -lt 0.999 -or $fallbackRate -ge 0.001 -or $overrunRate -ge 0.0001 -or
			$optOutRate -ge 0.10 -or $blindResponses -lt 25 -or $preferenceRate -lt 0.60) {
			throw "Auto default production thresholds are not satisfied."
		}
	}
}

$recipeSet = [string](Assert-ObjectProperty $aggregate 'recipeSetVersion' 'Telemetry aggregate export')
if ($recipeSet -cne $ExpectedRecipeSetVersion) {
	throw "Rollout aggregate recipe set does not match the promoted build."
}

Write-Host "Verified signed rollout qualification '$TargetStage' for '$ExpectedBuildId' from the exact signed aggregate export."
