[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$EvidencePath,

	[Parameter(Mandatory = $true)]
	[string]$SignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$PublicKeyHex,

	[Parameter(Mandatory = $true)]
	[ValidateSet("stable-opt-in", "auto-recommended", "auto-default")]
	[string]$TargetStage,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedBuildId,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedRecipeSetVersion,

	[int]$MaximumEvidenceAgeDays = 7,

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

$null = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $EvidencePath -SignaturePath $SignaturePath `
	-PublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath)) {
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

$evidence = Read-ReleaseJson -Path $evidenceFile.FullName
Assert-ExactProperties $evidence @(
	'domainRnnoiseTrack', 'generatedAtUtc', 'kind', 'population', 'preference', 'privacy',
	'recipeSetVersion', 'reliability', 'schemaVersion', 'sourceChannel', 'testedBuildIds', 'window'
) 'Rollout qualification'
if ([int](Assert-ObjectProperty $evidence 'schemaVersion' 'Rollout qualification') -ne 1 -or
	[string](Assert-ObjectProperty $evidence 'kind' 'Rollout qualification') -cne 'input-enhancement-rollout-qualification') {
	throw "Unsupported rollout qualification schema."
}

$generatedAt = Get-UtcTimestamp (Assert-ObjectProperty $evidence 'generatedAtUtc' 'Rollout qualification') 'generatedAtUtc'
$now = [DateTimeOffset]::UtcNow
if ($generatedAt -gt $now.AddMinutes(5) -or $generatedAt -lt $now.AddDays(-$MaximumEvidenceAgeDays)) {
	throw "Rollout qualification is future-dated or older than the allowed evidence age."
}

$window = Assert-ObjectProperty $evidence 'window' 'Rollout qualification'
Assert-ExactProperties $window @('endUtc', 'observationDays', 'startUtc') 'Rollout window'
$windowStart = Get-UtcTimestamp (Assert-ObjectProperty $window 'startUtc' 'Rollout window') 'window.startUtc'
$windowEnd = Get-UtcTimestamp (Assert-ObjectProperty $window 'endUtc' 'Rollout window') 'window.endUtc'
$observationDays = Get-WholeNumber (Assert-ObjectProperty $window 'observationDays' 'Rollout window') `
	'window.observationDays' 1
if ($windowStart -ge $windowEnd -or $windowEnd -gt $generatedAt -or
	($windowEnd - $windowStart).TotalDays -lt $observationDays) {
	throw "Rollout window is inconsistent with its attested observation days."
}

$sourceChannel = [string](Assert-ObjectProperty $evidence 'sourceChannel' 'Rollout qualification')
$requiredSourceChannel = if ($TargetStage -eq 'stable-opt-in') { 'preview' } else { 'stable' }
if ($sourceChannel -cne $requiredSourceChannel) {
	throw "$TargetStage requires evidence collected on the '$requiredSourceChannel' channel."
}
$testedBuildIds = @(Assert-ObjectProperty $evidence 'testedBuildIds' 'Rollout qualification')
if ($testedBuildIds.Count -lt 1 -or $testedBuildIds.Count -gt 16 -or
	@($testedBuildIds | Where-Object { [string]$_ -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' }).Count -gt 0 -or
	@($testedBuildIds | Select-Object -Unique).Count -ne $testedBuildIds.Count -or
	$ExpectedBuildId -cnotin @($testedBuildIds | ForEach-Object { [string]$_ })) {
	throw "Rollout qualification must uniquely include the exact immutable build being promoted."
}

$population = Assert-ObjectProperty $evidence 'population' 'Rollout qualification'
Assert-ExactProperties $population @('distinctDevices', 'distinctUsers', 'talkHours') 'Rollout population'
$users = Get-WholeNumber (Assert-ObjectProperty $population 'distinctUsers' 'Rollout population') 'population.distinctUsers'
$devices = Get-WholeNumber (Assert-ObjectProperty $population 'distinctDevices' 'Rollout population') 'population.distinctDevices'
$talkHours = Get-FiniteNumber (Assert-ObjectProperty $population 'talkHours' 'Rollout population') 'population.talkHours'

$reliability = Assert-ObjectProperty $evidence 'reliability' 'Rollout qualification'
Assert-ExactProperties $reliability @(
	'callbackOverrunFrameRate', 'crashFreeSessionRate', 'fallbackSessionRate', 'manualRollbackOrOptOutRate',
	'modelHashMismatchCount', 'p0Count', 'p1Count', 'recurrentCallbackRegressionCount'
) 'Rollout reliability'
$p0 = Get-WholeNumber (Assert-ObjectProperty $reliability 'p0Count' 'Rollout reliability') 'reliability.p0Count'
$p1 = Get-WholeNumber (Assert-ObjectProperty $reliability 'p1Count' 'Rollout reliability') 'reliability.p1Count'
$hashFailures = Get-WholeNumber (Assert-ObjectProperty $reliability 'modelHashMismatchCount' 'Rollout reliability') `
	'reliability.modelHashMismatchCount'
$callbackRegressions = Get-WholeNumber `
	(Assert-ObjectProperty $reliability 'recurrentCallbackRegressionCount' 'Rollout reliability') `
	'reliability.recurrentCallbackRegressionCount'
$crashFree = Get-FiniteNumber (Assert-ObjectProperty $reliability 'crashFreeSessionRate' 'Rollout reliability') `
	'reliability.crashFreeSessionRate' 0 1
$fallbackRate = Get-FiniteNumber (Assert-ObjectProperty $reliability 'fallbackSessionRate' 'Rollout reliability') `
	'reliability.fallbackSessionRate' 0 1
$overrunRate = Get-FiniteNumber (Assert-ObjectProperty $reliability 'callbackOverrunFrameRate' 'Rollout reliability') `
	'reliability.callbackOverrunFrameRate' 0 1
$optOutRate = Get-FiniteNumber (Assert-ObjectProperty $reliability 'manualRollbackOrOptOutRate' 'Rollout reliability') `
	'reliability.manualRollbackOrOptOutRate' 0 1
if ($p0 -ne 0 -or $p1 -ne 0 -or $hashFailures -ne 0 -or $callbackRegressions -ne 0) {
	throw "Rollout qualification contains a P0/P1, model-hash failure, or recurrent callback regression."
}

$preference = Assert-ObjectProperty $evidence 'preference' 'Rollout qualification'
Assert-ExactProperties $preference @('blindAbResponses', 'selectedOverOriginalRate') 'Rollout preference'
$blindResponses = Get-WholeNumber (Assert-ObjectProperty $preference 'blindAbResponses' 'Rollout preference') `
	'preference.blindAbResponses'
$preferenceRate = Get-FiniteNumber (Assert-ObjectProperty $preference 'selectedOverOriginalRate' 'Rollout preference') `
	'preference.selectedOverOriginalRate' 0 1

$domainTrack = Assert-ObjectProperty $evidence 'domainRnnoiseTrack' 'Rollout qualification'
Assert-ExactProperties $domainTrack @('outcome', 'status') 'Domain RNNoise track'
$domainStatus = [string](Assert-ObjectProperty $domainTrack 'status' 'Domain RNNoise track')
$domainOutcome = [string](Assert-ObjectProperty $domainTrack 'outcome' 'Domain RNNoise track')
if ($domainStatus -cnotin @('pending', 'completed') -or
	$domainOutcome -cnotin @('pending', 'embedded-retained', 'custom-promoted')) {
	throw "Domain RNNoise track has an unsupported status or outcome."
}
if (($domainStatus -eq 'completed' -and $domainOutcome -eq 'pending') -or
	($domainStatus -eq 'pending' -and $domainOutcome -ne 'pending')) {
	throw "Domain RNNoise track status and outcome are inconsistent."
}

$privacy = Assert-ObjectProperty $evidence 'privacy' 'Rollout qualification'
Assert-ExactProperties $privacy @(
	'optInOnly', 'rawAudioIncluded', 'rawDeviceIdsIncluded', 'retentionDays', 'transcriptsIncluded', 'voiceprintsIncluded'
) 'Rollout privacy'
if ((Assert-ObjectProperty $privacy 'optInOnly' 'Rollout privacy') -ne $true -or
	(Assert-ObjectProperty $privacy 'rawAudioIncluded' 'Rollout privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'rawDeviceIdsIncluded' 'Rollout privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'transcriptsIncluded' 'Rollout privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'voiceprintsIncluded' 'Rollout privacy') -ne $false -or
	(Get-WholeNumber (Assert-ObjectProperty $privacy 'retentionDays' 'Rollout privacy') 'privacy.retentionDays' 1) -gt 30) {
	throw "Rollout qualification violates the input-enhancement telemetry privacy contract."
}

switch ($TargetStage) {
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

$recipeSet = [string](Assert-ObjectProperty $evidence 'recipeSetVersion' 'Rollout qualification')
if ($recipeSet -cne $ExpectedRecipeSetVersion) {
	throw "Rollout qualification recipe set does not match the promoted build."
}

Write-Host "Verified signed rollout qualification '$TargetStage' for '$ExpectedBuildId'."
