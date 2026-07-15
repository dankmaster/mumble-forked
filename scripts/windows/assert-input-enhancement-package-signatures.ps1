[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$StageRoot,

	[Parameter(Mandatory = $true)]
	[string]$PublicKeyHex,

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$assertScript = Join-Path $PSScriptRoot "assert-input-enhancement-detached-signature.ps1"
foreach ($name in @('input-models.json', 'input-recipes.json')) {
	$path = Join-Path $StageRoot $name
	& $assertScript -InputPath $path -SignaturePath "$path.sig" `
		-PublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath
}
Write-Host "Verified signed input-enhancement package manifests under '$StageRoot'."
