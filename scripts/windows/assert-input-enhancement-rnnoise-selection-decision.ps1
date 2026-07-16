[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$DecisionPath,

	[Parameter(Mandatory = $true)]
	[string]$DecisionSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$PublicKeyHex,

	[string]$PythonPath = "python",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$normalizedKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $DecisionPath `
	-SignaturePath $DecisionSignaturePath -PublicKeyHex $normalizedKey -OpenSslPath $OpenSslPath)) {
	throw "RNNoise selection decision has no valid detached Ed25519 signature."
}

$decisionFile = Get-Item -LiteralPath $DecisionPath -ErrorAction Stop
$signatureFile = Get-Item -LiteralPath $DecisionSignaturePath -ErrorAction Stop
if ($decisionFile.Name -cne 'rnnoise-selection-decision.json' -or
	$signatureFile.Name -cne 'rnnoise-selection-decision.json.sig') {
	throw "RNNoise selection decision files must use the stable rnnoise-selection-decision.json[.sig] names."
}
if ($decisionFile.Length -le 0 -or $decisionFile.Length -gt 65536 -or $signatureFile.Length -ne 64) {
	throw "RNNoise selection decision or signature has an unsafe size."
}

Assert-StrictInputEnhancementRolloutJson -Path $decisionFile.FullName -Kind rnnoise-decision `
	-PythonPath $PythonPath
$decision = Read-ReleaseJson -Path $decisionFile.FullName
$selectionStatus = [string](Assert-ObjectProperty $decision 'status' 'RNNoise selection decision')
$rolloutOutcome = switch ($selectionStatus) {
	'embedded-retained' { 'embedded-retained' }
	'custom-selected' { 'custom-promoted' }
	default { throw "RNNoise selection decision has an unsupported status '$selectionStatus'." }
}

Write-Host "Verified signed RNNoise selection decision '$selectionStatus'."
[pscustomobject][ordered]@{
	selectionStatus = $selectionStatus
	rolloutOutcome = $rolloutOutcome
	fileName = $decisionFile.Name
	sha256 = Get-ReleaseFileSha256 -Path $decisionFile.FullName
	signatureFileName = $signatureFile.Name
	signatureSha256 = Get-ReleaseFileSha256 -Path $signatureFile.FullName
}
