[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$EvidencePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedCandidateExecutableSha256,
	[Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int]$ExpectedBuildNumber,
	[Parameter(Mandatory = $true)] [string]$ExpectedPublicKeyHex
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Assert-ExactProperties([object]$Object, [string[]]$Names, [string]$Context) {
	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	if (@(Compare-Object -ReferenceObject @($Names | Sort-Object) -DifferenceObject $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties."
	}
}

function Get-RawPublicKeySha256([string]$PublicKeyHex) {
	$normalized = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
	[byte[]]$bytes = [byte[]]::new(32)
	for ($index = 0; $index -lt $bytes.Length; $index++) {
		$bytes[$index] = [Convert]::ToByte($normalized.Substring($index * 2, 2), 16)
	}
	$sha = [Security.Cryptography.SHA256]::Create()
	try {
		return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
	} finally {
		$sha.Dispose()
	}
}

$evidence = Read-ReleaseJson -Path $EvidencePath
Assert-ExactProperties $evidence @(
	'audioFree', 'candidateExecutableSha256', 'createdAtUtc', 'generatorSha256', 'kind', 'passed',
	'runtimeDiagnosticBase64', 'runtimeDiagnosticSha256', 'schemaVersion'
) 'Embedded-key attestation'

$generatorPath = Join-Path $PSScriptRoot 'new-input-enhancement-embedded-key-attestation.ps1'
if ([int]$evidence.schemaVersion -ne 1 -or
	[string]$evidence.kind -cne 'input-enhancement-embedded-key-attestation' -or
	$evidence.passed -ne $true -or $evidence.audioFree -ne $true -or
	[string]$evidence.candidateExecutableSha256 -cne $ExpectedCandidateExecutableSha256 -or
	[string]$evidence.generatorSha256 -cne (Get-ReleaseFileSha256 -Path $generatorPath) -or
	[string]$evidence.runtimeDiagnosticSha256 -cnotmatch '^[0-9a-f]{64}$' -or
	[string]$evidence.runtimeDiagnosticBase64 -cnotmatch '^[A-Za-z0-9+/]+={0,2}$') {
	throw 'Embedded-key attestation identity or generator binding is invalid.'
}
$createdAt = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$evidence.createdAtUtc,
	[Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind,
	[ref]$createdAt)) {
	throw 'Embedded-key attestation timestamp is invalid.'
}

try {
	[byte[]]$diagnosticBytes = [Convert]::FromBase64String([string]$evidence.runtimeDiagnosticBase64)
} catch {
	throw 'Embedded-key runtime diagnostic is not canonical base64.'
}
if ([Convert]::ToBase64String($diagnosticBytes) -cne [string]$evidence.runtimeDiagnosticBase64) {
	throw 'Embedded-key runtime diagnostic base64 is not canonical.'
}
$sha = [Security.Cryptography.SHA256]::Create()
try {
	$diagnosticSha256 = ([BitConverter]::ToString($sha.ComputeHash($diagnosticBytes))).Replace('-', '').ToLowerInvariant()
} finally {
	$sha.Dispose()
}
if ($diagnosticSha256 -cne [string]$evidence.runtimeDiagnosticSha256) {
	throw 'Embedded-key runtime diagnostic bytes do not match their SHA-256.'
}
try {
	$diagnosticJson = [Text.UTF8Encoding]::new($false, $true).GetString($diagnosticBytes)
	$diagnostic = $diagnosticJson | ConvertFrom-Json
} catch {
	throw 'Embedded-key runtime diagnostic is not strict UTF-8 JSON.'
}
Assert-ExactProperties $diagnostic @(
	'buildNumber', 'configuredPublicKeySha256', 'kind', 'packageVerificationMode', 'schemaVersion'
) 'Embedded-key runtime diagnostic'

$expectedKeySha256 = Get-RawPublicKeySha256 -PublicKeyHex $ExpectedPublicKeyHex
if ([int]$diagnostic.schemaVersion -ne 1 -or
	[string]$diagnostic.kind -cne 'mumble-input-enhancement-build-identity' -or
	[int]$diagnostic.buildNumber -ne $ExpectedBuildNumber -or [int]$diagnostic.buildNumber -le 0 -or
	[string]$diagnostic.packageVerificationMode -cne 'managed-signed' -or
	[string]$diagnostic.configuredPublicKeySha256 -cne $expectedKeySha256) {
	throw 'Candidate is build-0/unmanaged or its embedded Ed25519 public key does not match the rehearsal key.'
}

Write-Host "Verified positive build $ExpectedBuildNumber and its embedded input-enhancement Ed25519 key."
