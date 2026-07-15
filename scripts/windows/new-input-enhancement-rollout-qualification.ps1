[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet("preview", "stable")]
	[string]$SourceChannel,

	[Parameter(Mandatory = $true)]
	[string[]]$TestedBuildIds,

	[Parameter(Mandatory = $true)]
	[string]$RecipeSetVersion,

	[Parameter(Mandatory = $true)]
	[DateTimeOffset]$WindowStartUtc,

	[Parameter(Mandatory = $true)]
	[DateTimeOffset]$WindowEndUtc,

	[Parameter(Mandatory = $true)]
	[int]$ObservationDays,

	[Parameter(Mandatory = $true)]
	[int]$DistinctUsers,

	[Parameter(Mandatory = $true)]
	[int]$DistinctDevices,

	[Parameter(Mandatory = $true)]
	[double]$TalkHours,

	[int]$P0Count = 0,
	[int]$P1Count = 0,
	[int]$ModelHashMismatchCount = 0,
	[int]$RecurrentCallbackRegressionCount = 0,
	[double]$CrashFreeSessionRate = 1.0,
	[double]$FallbackSessionRate = 0.0,
	[double]$CallbackOverrunFrameRate = 0.0,
	[double]$ManualRollbackOrOptOutRate = 0.0,
	[int]$BlindAbResponses = 0,
	[double]$SelectedOverOriginalRate = 0.0,

	[ValidateSet("pending", "completed")]
	[string]$DomainRnnoiseStatus = "pending",

	[ValidateSet("pending", "embedded-retained", "custom-promoted")]
	[string]$DomainRnnoiseOutcome = "pending",

	[int]$TelemetryRetentionDays = 30,

	[Parameter(Mandatory = $true)]
	[string]$PrivateKeyBase64,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedPublicKeyHex,

	[string]$OutputPath = "input-enhancement-rollout.json",

	[string]$SignaturePath = "",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if ($TestedBuildIds.Count -lt 1 -or $TestedBuildIds.Count -gt 16 -or
	@($TestedBuildIds | Where-Object { $_ -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' }).Count -gt 0 -or
	@($TestedBuildIds | Select-Object -Unique).Count -ne $TestedBuildIds.Count) {
	throw "TestedBuildIds must contain 1 to 16 unique immutable input-enhancement build IDs."
}
if ($RecipeSetVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
	throw "RecipeSetVersion is invalid."
}
if ($WindowStartUtc -ge $WindowEndUtc -or $WindowEndUtc -gt [DateTimeOffset]::UtcNow.AddMinutes(5) -or
	$ObservationDays -lt 1 -or ($WindowEndUtc - $WindowStartUtc).TotalDays -lt $ObservationDays) {
	throw "The rollout observation window is invalid."
}
foreach ($entry in @{
	DistinctUsers = $DistinctUsers; DistinctDevices = $DistinctDevices; P0Count = $P0Count; P1Count = $P1Count;
	ModelHashMismatchCount = $ModelHashMismatchCount;
	RecurrentCallbackRegressionCount = $RecurrentCallbackRegressionCount; BlindAbResponses = $BlindAbResponses
}.GetEnumerator()) {
	if ([int64]$entry.Value -lt 0) { throw "$($entry.Key) cannot be negative." }
}
foreach ($entry in @{
	CrashFreeSessionRate = $CrashFreeSessionRate; FallbackSessionRate = $FallbackSessionRate;
	CallbackOverrunFrameRate = $CallbackOverrunFrameRate;
	ManualRollbackOrOptOutRate = $ManualRollbackOrOptOutRate; SelectedOverOriginalRate = $SelectedOverOriginalRate
}.GetEnumerator()) {
	if ([double]::IsNaN([double]$entry.Value) -or [double]::IsInfinity([double]$entry.Value) -or
		[double]$entry.Value -lt 0 -or [double]$entry.Value -gt 1) {
		throw "$($entry.Key) must be between 0 and 1."
	}
}
if ([double]::IsNaN($TalkHours) -or [double]::IsInfinity($TalkHours) -or $TalkHours -lt 0) {
	throw "TalkHours must be finite and non-negative."
}
if ($TelemetryRetentionDays -lt 1 -or $TelemetryRetentionDays -gt 30) {
	throw "TelemetryRetentionDays must be between 1 and 30."
}
if (($DomainRnnoiseStatus -eq 'completed' -and $DomainRnnoiseOutcome -eq 'pending') -or
	($DomainRnnoiseStatus -eq 'pending' -and $DomainRnnoiseOutcome -ne 'pending')) {
	throw "Domain RNNoise status and outcome are inconsistent."
}
if ((Split-Path -Leaf $OutputPath) -cne 'input-enhancement-rollout.json') {
	throw "OutputPath must end in input-enhancement-rollout.json."
}
if ([string]::IsNullOrWhiteSpace($SignaturePath)) { $SignaturePath = "$OutputPath.sig" }
if ((Split-Path -Leaf $SignaturePath) -cne 'input-enhancement-rollout.json.sig') {
	throw "SignaturePath must end in input-enhancement-rollout.json.sig."
}

$generatedAt = [DateTimeOffset]::UtcNow
$evidence = [ordered]@{
	schemaVersion = 1
	kind = 'input-enhancement-rollout-qualification'
	generatedAtUtc = $generatedAt.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
	sourceChannel = $SourceChannel
	testedBuildIds = @($TestedBuildIds)
	recipeSetVersion = $RecipeSetVersion
	window = [ordered]@{
		startUtc = $WindowStartUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
		endUtc = $WindowEndUtc.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
		observationDays = $ObservationDays
	}
	population = [ordered]@{
		distinctUsers = $DistinctUsers
		distinctDevices = $DistinctDevices
		talkHours = $TalkHours
	}
	reliability = [ordered]@{
		p0Count = $P0Count
		p1Count = $P1Count
		modelHashMismatchCount = $ModelHashMismatchCount
		recurrentCallbackRegressionCount = $RecurrentCallbackRegressionCount
		crashFreeSessionRate = $CrashFreeSessionRate
		fallbackSessionRate = $FallbackSessionRate
		callbackOverrunFrameRate = $CallbackOverrunFrameRate
		manualRollbackOrOptOutRate = $ManualRollbackOrOptOutRate
	}
	preference = [ordered]@{
		blindAbResponses = $BlindAbResponses
		selectedOverOriginalRate = $SelectedOverOriginalRate
	}
	domainRnnoiseTrack = [ordered]@{
		status = $DomainRnnoiseStatus
		outcome = $DomainRnnoiseOutcome
	}
	privacy = [ordered]@{
		optInOnly = $true
		rawAudioIncluded = $false
		transcriptsIncluded = $false
		voiceprintsIncluded = $false
		rawDeviceIdsIncluded = $false
		retentionDays = $TelemetryRetentionDays
	}
}

Write-ReleaseJson -Value $evidence -Path $OutputPath
Protect-FileWithEd25519 -InputPath $OutputPath -SignaturePath $SignaturePath `
	-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $ExpectedPublicKeyHex -OpenSslPath $OpenSslPath
Write-Host "Created signed rollout qualification '$OutputPath' for $($TestedBuildIds.Count) immutable build(s)."
