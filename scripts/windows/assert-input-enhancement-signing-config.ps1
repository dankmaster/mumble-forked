[CmdletBinding()]
param(
	[string]$AzureClientId = $env:AZURE_CLIENT_ID,
	[string]$AzureTenantId = $env:AZURE_TENANT_ID,
	[string]$AzureSubscriptionId = $env:AZURE_SUBSCRIPTION_ID,
	[string]$Endpoint = $env:AZURE_ARTIFACT_SIGNING_ENDPOINT,
	[string]$SigningAccountName = $env:AZURE_ARTIFACT_SIGNING_ACCOUNT_NAME,
	[string]$CertificateProfileName = $env:AZURE_ARTIFACT_SIGNING_CERTIFICATE_PROFILE,
	[string]$ExpectedSignerSubject = $env:AZURE_ARTIFACT_SIGNING_EXPECTED_SUBJECT,
	[string]$Ed25519PrivateKeyBase64 = $env:INPUT_ENHANCEMENT_ED25519_PRIVATE_KEY_BASE64,
	[string]$Ed25519PublicKeyHex = $env:INPUT_ENHANCEMENT_ED25519_PUBLIC_KEY_HEX,
	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$required = [ordered]@{
	AZURE_CLIENT_ID                            = $AzureClientId
	AZURE_TENANT_ID                            = $AzureTenantId
	AZURE_SUBSCRIPTION_ID                      = $AzureSubscriptionId
	AZURE_ARTIFACT_SIGNING_ENDPOINT            = $Endpoint
	AZURE_ARTIFACT_SIGNING_ACCOUNT_NAME        = $SigningAccountName
	AZURE_ARTIFACT_SIGNING_CERTIFICATE_PROFILE = $CertificateProfileName
	AZURE_ARTIFACT_SIGNING_EXPECTED_SUBJECT    = $ExpectedSignerSubject
	INPUT_ENHANCEMENT_ED25519_PRIVATE_KEY_BASE64 = $Ed25519PrivateKeyBase64
	INPUT_ENHANCEMENT_ED25519_PUBLIC_KEY_HEX     = $Ed25519PublicKeyHex
}
$missing = @($required.GetEnumerator() | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.Value) } | ForEach-Object { $_.Key })
if ($missing.Count -gt 0) {
	throw "Artifact Signing is required and is not configured. Missing protected environment values: $($missing -join ', ')."
}

foreach ($entry in @(
	@("AZURE_CLIENT_ID", $AzureClientId),
	@("AZURE_TENANT_ID", $AzureTenantId),
	@("AZURE_SUBSCRIPTION_ID", $AzureSubscriptionId)
)) {
	$parsedGuid = [guid]::Empty
	if (-not [guid]::TryParse([string]$entry[1], [ref]$parsedGuid) -or $parsedGuid -eq [guid]::Empty) {
		throw "$($entry[0]) must be a non-empty GUID."
	}
}

$endpointUri = $null
if (-not [uri]::TryCreate($Endpoint, [System.UriKind]::Absolute, [ref]$endpointUri) -or $endpointUri.Scheme -ne "https") {
	throw "AZURE_ARTIFACT_SIGNING_ENDPOINT must be an absolute HTTPS URI."
}
foreach ($entry in @(
	@("AZURE_ARTIFACT_SIGNING_ACCOUNT_NAME", $SigningAccountName),
	@("AZURE_ARTIFACT_SIGNING_CERTIFICATE_PROFILE", $CertificateProfileName)
)) {
	if ([string]$entry[1] -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{1,126}[A-Za-z0-9]$') {
		throw "$($entry[0]) contains unsupported characters or has an unsafe length."
	}
}

$publicKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $Ed25519PublicKeyHex `
	-Context "INPUT_ENHANCEMENT_ED25519_PUBLIC_KEY_HEX"
$derivedPublicKey = Get-Ed25519PublicKeyHexFromPrivateKey `
	-PrivateKeyBase64 $Ed25519PrivateKeyBase64 -OpenSslPath $OpenSslPath
if ($derivedPublicKey -cne $publicKey) {
	throw "The protected input-enhancement Ed25519 private key does not match its configured public key."
}

Write-Host "Azure Artifact Signing and Ed25519 release configuration are present, match, and are structurally valid."
