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

	[int]$MaximumAggregateAgeDays = 7,

	[string]$RnnoiseDecisionPath = "",

	[string]$RnnoiseDecisionSignaturePath = "",

	[Parameter(Mandatory = $true)]
	[string]$PrivateKeyBase64,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedPublicKeyHex,

	[string]$OutputPath = "input-enhancement-rollout.json",

	[string]$SignaturePath = "",

	[string]$PythonPath = "python",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$aggregateKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $AggregatePublicKeyHex
$releaseKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $ExpectedPublicKeyHex
if ($aggregateKey -ceq $releaseKey) {
	throw "Telemetry aggregate exports and rollout envelopes must use separate Ed25519 signer identities."
}
if ((Split-Path -Leaf $OutputPath) -cne 'input-enhancement-rollout.json') {
	throw "OutputPath must end in input-enhancement-rollout.json."
}
if ([string]::IsNullOrWhiteSpace($SignaturePath)) { $SignaturePath = "$OutputPath.sig" }
if ((Split-Path -Leaf $SignaturePath) -cne 'input-enhancement-rollout.json.sig') {
	throw "SignaturePath must end in input-enhancement-rollout.json.sig."
}

# This verifier checks the exporter signature, the pinned query hash and the
# canonical window/filter hash before any rollout fields are copied. There is
# deliberately no CLI path for supplying population or reliability totals.
& (Join-Path $PSScriptRoot 'assert-input-enhancement-aggregate-export.ps1') `
	-AggregateExportPath $AggregateExportPath `
	-AggregateExportSignaturePath $AggregateExportSignaturePath `
	-AggregatePublicKeyHex $AggregatePublicKeyHex `
	-ExpectedQuerySha256 $ExpectedQuerySha256 `
	-MaximumEvidenceAgeDays $MaximumAggregateAgeDays `
	-PythonPath $PythonPath `
	-OpenSslPath $OpenSslPath

$aggregate = Read-ReleaseJson -Path $AggregateExportPath
$aggregateFile = Get-Item -LiteralPath $AggregateExportPath -ErrorAction Stop
$aggregateSignatureFile = Get-Item -LiteralPath $AggregateExportSignaturePath -ErrorAction Stop
$query = Assert-ObjectProperty $aggregate 'query' 'Telemetry aggregate export'

$hasDecision = -not [string]::IsNullOrWhiteSpace($RnnoiseDecisionPath)
$hasDecisionSignature = -not [string]::IsNullOrWhiteSpace($RnnoiseDecisionSignaturePath)
if ($hasDecision -xor $hasDecisionSignature) {
	throw "RNNoise completion requires both the signed selection decision and its detached signature."
}
$domainTrack = [ordered]@{ status = 'pending' }
if ($hasDecision) {
	$decisionEvidence = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rnnoise-selection-decision.ps1') `
		-DecisionPath $RnnoiseDecisionPath `
		-DecisionSignaturePath $RnnoiseDecisionSignaturePath `
		-PublicKeyHex $releaseKey `
		-PythonPath $PythonPath `
		-OpenSslPath $OpenSslPath
	$domainTrack = [ordered]@{
		status = 'completed'
		outcome = [string]$decisionEvidence.rolloutOutcome
		decision = [ordered]@{
			fileName = [string]$decisionEvidence.fileName
			sha256 = [string]$decisionEvidence.sha256
			signatureFileName = [string]$decisionEvidence.signatureFileName
			signatureSha256 = [string]$decisionEvidence.signatureSha256
		}
	}
}

$evidence = [ordered]@{
	schemaVersion = 2
	kind = 'input-enhancement-rollout-qualification'
	generatedAtUtc = [DateTimeOffset]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
	sourceAggregate = [ordered]@{
		fileName = $aggregateFile.Name
		sha256 = Get-ReleaseFileSha256 -Path $aggregateFile.FullName
		signatureFileName = $aggregateSignatureFile.Name
		signatureSha256 = Get-ReleaseFileSha256 -Path $aggregateSignatureFile.FullName
		querySha256 = [string]$query.sha256
		windowSha256 = [string]$query.windowSha256
	}
	domainRnnoiseTrack = $domainTrack
}

Write-ReleaseJson -Value $evidence -Path $OutputPath
Assert-StrictInputEnhancementRolloutJson -Path $OutputPath -Kind rollout -PythonPath $PythonPath
Protect-FileWithEd25519 -InputPath $OutputPath -SignaturePath $SignaturePath `
	-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $ExpectedPublicKeyHex -OpenSslPath $OpenSslPath
Write-Host "Created signed rollout qualification '$OutputPath' from hash-bound aggregate export '$($aggregateFile.Name)'."
