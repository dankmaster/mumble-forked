[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$AggregateExportPath,

	[Parameter(Mandatory = $true)]
	[string]$AggregateExportSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$AggregatePublicKeyHex,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedQuerySha256,

	[int]$MaximumEvidenceAgeDays = 7,

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

function Get-Utf8Sha256 {
	param([Parameter(Mandatory = $true)][string]$Value)
	$sha = [Security.Cryptography.SHA256]::Create()
	try {
		$bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
		return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
	} finally {
		$sha.Dispose()
	}
}

function Get-WindowBindingSha256 {
	param([Parameter(Mandatory = $true)][object]$Aggregate)
	$window = Assert-ObjectProperty $Aggregate 'window' 'Aggregate export'
	$query = Assert-ObjectProperty $Aggregate 'query' 'Aggregate export'
	$buildIds = @((Assert-ObjectProperty $Aggregate 'testedBuildIds' 'Aggregate export') | ForEach-Object { [string]$_ })
	$startUtc = (Get-UtcTimestamp (Assert-ObjectProperty $window 'startUtc' 'Aggregate window') `
		'aggregate.window.startUtc').ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
	$endUtc = (Get-UtcTimestamp (Assert-ObjectProperty $window 'endUtc' 'Aggregate window') `
		'aggregate.window.endUtc').ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
	$canonical = @(
		'input-enhancement-rollout-window-v2',
		"querySha256=$([string](Assert-ObjectProperty $query 'sha256' 'Aggregate query'))",
		"sourceSnapshotSha256=$([string](Assert-ObjectProperty $query 'sourceSnapshotSha256' 'Aggregate query'))",
		"startUtc=$startUtc",
		"endUtc=$endUtc",
		"observationDays=$([string](Assert-ObjectProperty $window 'observationDays' 'Aggregate window'))",
		"sourceChannel=$([string](Assert-ObjectProperty $Aggregate 'sourceChannel' 'Aggregate export'))",
		"rolloutAudience=$([string](Assert-ObjectProperty $Aggregate 'rolloutAudience' 'Aggregate export'))",
		"recipeSetVersion=$([string](Assert-ObjectProperty $Aggregate 'recipeSetVersion' 'Aggregate export'))",
		"testedBuildIds=$([string]::Join(',', $buildIds))"
	) -join "`n"
	return Get-Utf8Sha256 -Value "$canonical`n"
}

if ($MaximumEvidenceAgeDays -lt 1 -or $MaximumEvidenceAgeDays -gt 30) {
	throw "MaximumEvidenceAgeDays must be between 1 and 30."
}
$normalizedAggregateKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $AggregatePublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $AggregateExportPath `
	-SignaturePath $AggregateExportSignaturePath -PublicKeyHex $normalizedAggregateKey -OpenSslPath $OpenSslPath)) {
	throw "Telemetry aggregate export has no valid detached Ed25519 signature."
}
$exportFile = Get-Item -LiteralPath $AggregateExportPath -ErrorAction Stop
$signatureFile = Get-Item -LiteralPath $AggregateExportSignaturePath -ErrorAction Stop
if ($exportFile.Name -cne 'input-enhancement-aggregate-export.json' -or
	$signatureFile.Name -cne 'input-enhancement-aggregate-export.json.sig') {
	throw "Telemetry aggregate export files must use the stable input-enhancement-aggregate-export.json[.sig] names."
}
if ($exportFile.Length -le 0 -or $exportFile.Length -gt 65536 -or $signatureFile.Length -ne 64) {
	throw "Telemetry aggregate export or signature has an unsafe size."
}

Assert-StrictInputEnhancementRolloutJson -Path $exportFile.FullName -Kind aggregate -PythonPath $PythonPath

$aggregate = Read-ReleaseJson -Path $exportFile.FullName
Assert-ExactProperties $aggregate @(
	'generatedAtUtc', 'kind', 'population', 'preference', 'privacy', 'query', 'recipeSetVersion',
	'reliability', 'rolloutAudience', 'schemaVersion', 'sourceChannel', 'testedBuildIds', 'window'
) 'Telemetry aggregate export'
if ([int](Assert-ObjectProperty $aggregate 'schemaVersion' 'Telemetry aggregate export') -ne 2 -or
	[string](Assert-ObjectProperty $aggregate 'kind' 'Telemetry aggregate export') -cne
		'input-enhancement-telemetry-aggregate-export') {
	throw "Unsupported telemetry aggregate export schema."
}

$generatedAt = Get-UtcTimestamp (Assert-ObjectProperty $aggregate 'generatedAtUtc' 'Telemetry aggregate export') `
	'aggregate.generatedAtUtc'
$now = [DateTimeOffset]::UtcNow
if ($generatedAt -gt $now.AddMinutes(5) -or $generatedAt -lt $now.AddDays(-$MaximumEvidenceAgeDays)) {
	throw "Telemetry aggregate export is future-dated or older than the allowed evidence age."
}

$sourceChannel = [string](Assert-ObjectProperty $aggregate 'sourceChannel' 'Telemetry aggregate export')
if ($sourceChannel -cnotin @('preview', 'stable')) {
	throw "Telemetry aggregate export sourceChannel is invalid."
}
$rolloutAudience = [string](Assert-ObjectProperty $aggregate 'rolloutAudience' 'Telemetry aggregate export')
if ($rolloutAudience -cnotin @('private-community', 'public')) {
	throw "Telemetry aggregate export rolloutAudience is invalid."
}
$recipeSetVersion = [string](Assert-ObjectProperty $aggregate 'recipeSetVersion' 'Telemetry aggregate export')
if ($recipeSetVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
	throw "Telemetry aggregate export recipeSetVersion is invalid."
}
$testedBuildIds = @((Assert-ObjectProperty $aggregate 'testedBuildIds' 'Telemetry aggregate export') | ForEach-Object { [string]$_ })
$sortedBuildIds = @($testedBuildIds)
[Array]::Sort($sortedBuildIds, [StringComparer]::Ordinal)
if ($testedBuildIds.Count -ne 1 -or
	@($testedBuildIds | Where-Object { $_ -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' }).Count -gt 0 -or
	@($testedBuildIds | Select-Object -Unique).Count -ne $testedBuildIds.Count -or
	@(Compare-Object $testedBuildIds $sortedBuildIds -SyncWindow 0).Count -ne 0) {
	throw "Telemetry aggregate export must contain exactly one immutable build ID."
}

$window = Assert-ObjectProperty $aggregate 'window' 'Telemetry aggregate export'
Assert-ExactProperties $window @('endUtc', 'observationDays', 'startUtc') 'Aggregate window'
$windowStart = Get-UtcTimestamp (Assert-ObjectProperty $window 'startUtc' 'Aggregate window') 'aggregate.window.startUtc'
$windowEnd = Get-UtcTimestamp (Assert-ObjectProperty $window 'endUtc' 'Aggregate window') 'aggregate.window.endUtc'
$observationDays = Get-WholeNumber (Assert-ObjectProperty $window 'observationDays' 'Aggregate window') `
	'aggregate.window.observationDays' 1
if ($windowStart -ge $windowEnd -or $windowEnd -gt $generatedAt -or
	($windowEnd - $windowStart).TotalDays -lt $observationDays -or
	($generatedAt - $windowEnd).TotalHours -gt 24 -or
	$windowEnd -lt $now.AddDays(-$MaximumEvidenceAgeDays)) {
	throw "Telemetry aggregate export window is inconsistent."
}

$query = Assert-ObjectProperty $aggregate 'query' 'Telemetry aggregate export'
Assert-ExactProperties $query @('id', 'sha256', 'sourceEventCount', 'sourceSnapshotSha256', 'windowSha256') `
	'Aggregate query'
if ([string](Assert-ObjectProperty $query 'id' 'Aggregate query') -cne 'input-enhancement-rollout-v2' -or
	[string](Assert-ObjectProperty $query 'sha256' 'Aggregate query') -cne $ExpectedQuerySha256) {
	throw "Telemetry aggregate export did not use the pinned rollout query."
}
foreach ($name in @('sha256', 'sourceSnapshotSha256', 'windowSha256')) {
	if ([string](Assert-ObjectProperty $query $name 'Aggregate query') -cnotmatch '^[0-9a-f]{64}$') {
		throw "Aggregate query '$name' is not a lowercase SHA-256."
	}
}
$null = Get-WholeNumber (Assert-ObjectProperty $query 'sourceEventCount' 'Aggregate query') `
	'aggregate.query.sourceEventCount'
$expectedWindowHash = Get-WindowBindingSha256 -Aggregate $aggregate
if ([string]$query.windowSha256 -cne $expectedWindowHash) {
	throw "Telemetry aggregate export window SHA-256 does not match its canonical query filters."
}

$population = Assert-ObjectProperty $aggregate 'population' 'Telemetry aggregate export'
Assert-ExactProperties $population @('distinctDevices', 'distinctUsers', 'intendedCommunityDevices', 'talkHours') `
	'Aggregate population'
$null = Get-WholeNumber (Assert-ObjectProperty $population 'distinctUsers' 'Aggregate population') `
	'aggregate.population.distinctUsers'
$null = Get-WholeNumber (Assert-ObjectProperty $population 'distinctDevices' 'Aggregate population') `
	'aggregate.population.distinctDevices'
$null = Get-WholeNumber (Assert-ObjectProperty $population 'intendedCommunityDevices' 'Aggregate population') `
	'aggregate.population.intendedCommunityDevices' 1
$null = Get-FiniteNumber (Assert-ObjectProperty $population 'talkHours' 'Aggregate population') `
	'aggregate.population.talkHours'

$reliability = Assert-ObjectProperty $aggregate 'reliability' 'Telemetry aggregate export'
Assert-ExactProperties $reliability @(
	'callbackOverrunFrameRate', 'crashFreeSessionRate', 'fallbackSessionRate', 'manualRollbackOrOptOutRate',
	'modelHashMismatchCount', 'p0Count', 'p1Count', 'recurrentCallbackRegressionCount'
) 'Aggregate reliability'
foreach ($name in @('p0Count', 'p1Count', 'modelHashMismatchCount', 'recurrentCallbackRegressionCount')) {
	$null = Get-WholeNumber (Assert-ObjectProperty $reliability $name 'Aggregate reliability') `
		"aggregate.reliability.$name"
}
foreach ($name in @('crashFreeSessionRate', 'fallbackSessionRate', 'callbackOverrunFrameRate', 'manualRollbackOrOptOutRate')) {
	$null = Get-FiniteNumber (Assert-ObjectProperty $reliability $name 'Aggregate reliability') `
		"aggregate.reliability.$name" 0 1
}

$preference = Assert-ObjectProperty $aggregate 'preference' 'Telemetry aggregate export'
Assert-ExactProperties $preference @('blindAbResponses', 'selectedOverOriginalRate') 'Aggregate preference'
$null = Get-WholeNumber (Assert-ObjectProperty $preference 'blindAbResponses' 'Aggregate preference') `
	'aggregate.preference.blindAbResponses'
$null = Get-FiniteNumber (Assert-ObjectProperty $preference 'selectedOverOriginalRate' 'Aggregate preference') `
	'aggregate.preference.selectedOverOriginalRate' 0 1

$privacy = Assert-ObjectProperty $aggregate 'privacy' 'Telemetry aggregate export'
Assert-ExactProperties $privacy @(
	'optInOnly', 'rawAudioIncluded', 'rawDeviceIdsIncluded', 'retentionDays', 'transcriptsIncluded', 'voiceprintsIncluded'
) 'Aggregate privacy'
if ((Assert-ObjectProperty $privacy 'optInOnly' 'Aggregate privacy') -ne $true -or
	(Assert-ObjectProperty $privacy 'rawAudioIncluded' 'Aggregate privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'rawDeviceIdsIncluded' 'Aggregate privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'transcriptsIncluded' 'Aggregate privacy') -ne $false -or
	(Assert-ObjectProperty $privacy 'voiceprintsIncluded' 'Aggregate privacy') -ne $false -or
	(Get-WholeNumber (Assert-ObjectProperty $privacy 'retentionDays' 'Aggregate privacy') `
		'aggregate.privacy.retentionDays' 1) -gt 30) {
	throw "Telemetry aggregate export violates the input-enhancement privacy contract."
}

Write-Host "Verified signed telemetry aggregate export for the one immutable build, audience '$rolloutAudience', query $ExpectedQuerySha256."
