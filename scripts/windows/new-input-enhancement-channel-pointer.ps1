[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet("preview", "stable")]
	[string]$Channel,

	[Parameter(Mandatory = $true)]
	[string]$Repository,

	[Parameter(Mandatory = $true)]
	[string]$QualificationPath,

	[Parameter(Mandatory = $true)]
	[string]$ReleaseSmokePath,

	[Parameter(Mandatory = $true)]
	[string]$Announcement,

	[string]$PreviousPointerPath = "",

	[string]$PreviousPointerSignaturePath = "",

	[Parameter(Mandatory = $true)]
	[string]$PrivateKeyBase64,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedPublicKeyHex,

	[Parameter(Mandatory = $true)]
	[string]$PolicyPath,

	[Parameter(Mandatory = $true)]
	[string]$PolicySignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$OutputPath,

	[string]$SignaturePath = "",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
	throw "Repository must use the GitHub owner/name form."
}
$normalizedAnnouncement = $Announcement.Trim()
if ($normalizedAnnouncement.Length -lt 1 -or $normalizedAnnouncement.Length -gt 2048) {
	throw "Channel announcement must contain 1 to 2048 trimmed characters."
}

$qualification = Read-ReleaseJson -Path $QualificationPath
$buildId = [string](Assert-ObjectProperty -Object $qualification -Name "buildId" -Context "Qualification")
$immutableTag = [string](Assert-ObjectProperty -Object $qualification -Name "immutableTag" -Context "Qualification")
if ($buildId -cne $immutableTag -or $buildId -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$') {
	throw "Qualification does not identify a valid immutable build tag."
}
$source = Assert-ObjectProperty -Object $qualification -Name "source" -Context "Qualification"
if ((Assert-ObjectProperty -Object $source -Name "dirty" -Context "Qualification source") -ne $false) {
	throw "Channel promotion refuses a dirty source qualification."
}
$artifact = Assert-ObjectProperty -Object $qualification -Name "artifact" -Context "Qualification"
$updatePackage = Assert-ObjectProperty -Object $qualification -Name "updatePackage" -Context "Qualification"
$signing = Assert-ObjectProperty -Object $qualification -Name "signing" -Context "Qualification"
if ((Assert-ObjectProperty -Object $artifact -Name "signed" -Context "Qualified artifact") -ne $true -or
	(Assert-ObjectProperty -Object $signing -Name "verified" -Context "Qualified signing") -ne $true) {
	throw "Channel promotion refuses an unsigned or unverified artifact."
}

$knownGood = New-Object System.Collections.Generic.List[string]
$knownGood.Add($immutableTag)
$publicKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $ExpectedPublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $PolicyPath -SignaturePath $PolicySignaturePath `
	-PublicKeyHex $publicKey -OpenSslPath $OpenSslPath)) {
	throw "Channel promotion requires a valid detached Ed25519 signature for its policy."
}
$policyFile = Get-Item -LiteralPath $PolicyPath -ErrorAction Stop
$policySignatureFile = Get-Item -LiteralPath $PolicySignaturePath -ErrorAction Stop
if ($policyFile.Name -cne 'input-enhancement-policy.json' -or
	$policySignatureFile.Name -cne 'input-enhancement-policy.json.sig') {
	throw "Signed channel policy files must use the stable input-enhancement-policy.json[.sig] names."
}
$null = Assert-CanonicalInputEnhancementPolicy -Path $policyFile.FullName `
	-ExpectedMinBuild ([uint64]$qualification.buildNumber) `
	-ExpectedRecipeSetVersion ([string]$qualification.recipeManifest.catalogRevision) `
	-RequireCurrentlyValid
if (-not [string]::IsNullOrWhiteSpace($PreviousPointerPath) -and (Test-Path -LiteralPath $PreviousPointerPath -PathType Leaf)) {
	if ([string]::IsNullOrWhiteSpace($PreviousPointerSignaturePath) -or
		-not (Test-Ed25519DetachedSignature -InputPath $PreviousPointerPath `
			-SignaturePath $PreviousPointerSignaturePath -PublicKeyHex $publicKey -OpenSslPath $OpenSslPath)) {
		throw "Previous channel pointer has no valid detached Ed25519 signature."
	}
	$previous = Read-ReleaseJson -Path $PreviousPointerPath
	if ([string](Assert-ObjectProperty -Object $previous -Name "channel" -Context "Previous channel pointer") -cne $Channel) {
		throw "Previous pointer belongs to a different channel."
	}
	$previousImmutableTag = [string](Assert-ObjectProperty -Object $previous -Name "immutableTag" -Context "Previous channel pointer")
	$previousKnownGoodTags = @(Assert-ObjectProperty -Object $previous -Name "knownGoodTags" -Context "Previous channel pointer")
	if ($previousKnownGoodTags.Count -lt 1 -or $previousKnownGoodTags.Count -gt 2 -or
		[string]$previousKnownGoodTags[0] -cne $previousImmutableTag -or
		@($previousKnownGoodTags | Select-Object -Unique).Count -ne $previousKnownGoodTags.Count) {
		throw "Previous pointer must lead with its current immutable build and contain at most two unique recovery tags."
	}
	foreach ($tag in $previousKnownGoodTags) {
		$tagString = [string]$tag
		if ($tagString -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$') {
			throw "Previous pointer contains invalid immutable tag '$tagString'."
		}
		if (-not $knownGood.Contains($tagString) -and $knownGood.Count -lt 2) {
			$knownGood.Add($tagString)
		}
	}
}

$artifactName = [string](Assert-ObjectProperty -Object $updatePackage -Name "fileName" -Context "Qualified update package")
if ($artifactName -notmatch '^[A-Za-z0-9._-]+\.mumble-update$' -or
	[string](Assert-ObjectProperty -Object $updatePackage -Name "format" -Context "Qualified update package") -cne "mumble-update-v1") {
	throw "Channel pointer requires a qualified mumble-update-v1 package."
}
$pointer = [ordered]@{
	schemaVersion       = 1
	channel             = $Channel
	channelTag          = "mumble-forked-$Channel"
	immutableTag        = $immutableTag
	buildId             = $buildId
	buildNumber         = [int]$qualification.buildNumber
	sourceSha           = [string]$source.sha
	artifact            = [ordered]@{
		fileName = $artifactName
		sha256   = [string]$updatePackage.sha256
		size     = [int64]$updatePackage.size
		url      = "https://github.com/$Repository/releases/download/$immutableTag/$artifactName"
	}
	qualification       = [ordered]@{
		sha256 = Get-ReleaseFileSha256 -Path $QualificationPath
		url    = "https://github.com/$Repository/releases/download/$immutableTag/qualification.json"
	}
	releaseSmoke        = [ordered]@{
		sha256 = Get-ReleaseFileSha256 -Path $ReleaseSmokePath
		url    = "https://github.com/$Repository/releases/download/$immutableTag/release-smoke.json"
	}
	modelManifestSha256 = [string]$qualification.modelManifest.sha256
	recipeManifestSha256 = [string]$qualification.recipeManifest.sha256
	inputEnhancementPolicy = [ordered]@{
		fileName          = $policyFile.Name
		sha256            = Get-ReleaseFileSha256 -Path $policyFile.FullName
		signatureFileName = $policySignatureFile.Name
		signatureSha256   = Get-ReleaseFileSha256 -Path $policySignatureFile.FullName
		url               = "https://github.com/$Repository/releases/download/mumble-forked-$Channel/$($policyFile.Name)"
	}
	detachedSignature   = [ordered]@{
		algorithm    = "Ed25519"
		encoding     = "raw"
		fileName     = "channel-pointer.json.sig"
		publicKeyHex = $publicKey
	}
	knownGoodTags       = $knownGood.ToArray()
	announcement        = $normalizedAnnouncement
	promotedAtUtc       = (Get-Date).ToUniversalTime().ToString("o")
}
Write-ReleaseJson -Value $pointer -Path $OutputPath
$expectedSignatureName = "$((Get-Item -LiteralPath $OutputPath).Name).sig"
if ([string]::IsNullOrWhiteSpace($SignaturePath)) {
	$SignaturePath = "$OutputPath.sig"
}
if ((Split-Path -Leaf $SignaturePath) -cne $expectedSignatureName) {
	throw "Channel pointer signature must be named '$expectedSignatureName'."
}
Protect-FileWithEd25519 -InputPath $OutputPath -SignaturePath $SignaturePath `
	-PrivateKeyBase64 $PrivateKeyBase64 -ExpectedPublicKeyHex $publicKey -OpenSslPath $OpenSslPath
Write-Host "Created '$Channel' pointer for immutable build '$immutableTag'; preserved $($knownGood.Count) known-good build(s)."
