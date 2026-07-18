[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[bool]$Available,

	[Parameter(Mandatory = $true)]
	[bool]$ForceOriginal,

	[Parameter(Mandatory = $true)]
	[ValidateSet('Original', 'Light', 'Balanced', 'Quality', 'Auto')]
	[string]$RecommendedProfile,

	[Parameter(Mandatory = $true)]
	[string]$RecipeSetVersion,

	[Parameter(Mandatory = $true)]
	[uint64]$MinBuild,

	[Parameter(Mandatory = $true)]
	[DateTimeOffset]$ExpiresAtUtc,

	[Parameter(Mandatory = $true)]
	[string]$PrivateKeyBase64,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedPublicKeyHex,

	[Parameter(Mandatory = $true)]
	[string]$OutputPath,

	[string]$SignaturePath = "",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if ($RecipeSetVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
	throw "RecipeSetVersion is not valid for the strict signed policy schema."
}
if ($MinBuild -gt 9007199254740991) {
	throw "MinBuild exceeds the largest exactly representable JSON integer."
}
$expiration = $ExpiresAtUtc.ToUniversalTime()
if ($expiration.Ticks % [TimeSpan]::TicksPerSecond -ne 0) {
	throw "ExpiresAtUtc must have whole-second precision."
}
if ($expiration -le [DateTimeOffset]::UtcNow) {
	throw "ExpiresAtUtc must be in the future."
}

# This byte layout deliberately mirrors canonicalPolicyBytes(): lexical key
# ordering, compact JSON, ASCII schema values and no trailing newline/BOM.
$availableText = if ($Available) { 'true' } else { 'false' }
$forceText = if ($ForceOriginal) { 'true' } else { 'false' }
$expirationText = $expiration.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
$canonical = '{"available":' + $availableText +
	',"expiresAt":"' + $expirationText +
	'","forceOriginal":' + $forceText +
	',"minBuild":' + $MinBuild.ToString([Globalization.CultureInfo]::InvariantCulture) +
	',"recipeSetVersion":"' + $RecipeSetVersion +
	'","recommendedProfile":"' + $RecommendedProfile + '"}'

$parent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
	New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
[IO.File]::WriteAllText($OutputPath, $canonical, [Text.UTF8Encoding]::new($false))
$null = Assert-CanonicalInputEnhancementPolicy -Path $OutputPath `
	-ExpectedMinBuild $MinBuild -ExpectedRecipeSetVersion $RecipeSetVersion -RequireCurrentlyValid
if ([string]::IsNullOrWhiteSpace($SignaturePath)) {
	$SignaturePath = "$OutputPath.sig"
}
& (Join-Path $PSScriptRoot 'protect-input-enhancement-json.ps1') `
	-InputPath $OutputPath -SignaturePath $SignaturePath `
	-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $ExpectedPublicKeyHex `
	-OpenSslPath $OpenSslPath
Write-Host "Created canonical signed input-enhancement policy '$OutputPath'."
