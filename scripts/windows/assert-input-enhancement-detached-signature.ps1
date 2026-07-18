[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$InputPath,

	[Parameter(Mandatory = $true)]
	[string]$SignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$PublicKeyHex,

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$null = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $InputPath -SignaturePath $SignaturePath `
	-PublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath)) {
	throw "Detached Ed25519 signature verification failed for '$InputPath'."
}
Write-Host "Verified detached Ed25519 signature for '$InputPath'."
