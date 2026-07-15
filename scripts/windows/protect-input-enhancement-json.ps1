[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$InputPath,

	[Parameter(Mandatory = $true)]
	[string]$SignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$PrivateKeyBase64,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedPublicKeyHex,

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

try {
	Get-Content -LiteralPath $InputPath -Raw -ErrorAction Stop | ConvertFrom-Json | Out-Null
} catch {
	throw "Refusing to sign invalid JSON '$InputPath': $($_.Exception.Message)"
}

Protect-FileWithEd25519 -InputPath $InputPath -SignaturePath $SignaturePath `
	-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $ExpectedPublicKeyHex `
	-OpenSslPath $OpenSslPath

Write-Host "Created and reverified detached Ed25519 signature for '$InputPath'."
