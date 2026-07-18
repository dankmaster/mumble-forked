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

	[string]$BootstrapRecoverySetPath = "",

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
$recoveryInstallers = New-Object System.Collections.Generic.List[object]
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
if (-not [string]::IsNullOrWhiteSpace($PreviousPointerPath)) {
	if (-not (Test-Path -LiteralPath $PreviousPointerPath -PathType Leaf)) {
		throw "Previous channel pointer path does not exist."
	}
	if ([string]::IsNullOrWhiteSpace($PreviousPointerSignaturePath) -or
		-not (Test-Ed25519DetachedSignature -InputPath $PreviousPointerPath `
			-SignaturePath $PreviousPointerSignaturePath -PublicKeyHex $publicKey -OpenSslPath $OpenSslPath)) {
		throw "Previous channel pointer has no valid detached Ed25519 signature."
	}
	$previous = Read-ReleaseJson -Path $PreviousPointerPath
	if ([string](Assert-ObjectProperty -Object $previous -Name "channel" -Context "Previous channel pointer") -cne $Channel) {
		throw "Previous pointer belongs to a different channel."
	}
	$previousSchema = [int](Assert-ObjectProperty -Object $previous -Name "schemaVersion" -Context "Previous channel pointer")
	if ($previousSchema -eq 2) {
		$previousImmutableTag = [string](Assert-ObjectProperty -Object $previous -Name "immutableTag" -Context "Previous channel pointer")
		$previousInstaller = Assert-ObjectProperty -Object $previous -Name "installer" -Context "Previous channel pointer"
		$previousRecovery = @(Assert-ObjectProperty -Object $previous -Name "recoveryInstallers" -Context "Previous channel pointer")
		$candidates = @(
			[pscustomobject]@{
				immutableTag = $previousImmutableTag
				fileName = [string](Assert-ObjectProperty $previousInstaller 'fileName' 'Previous candidate MSI')
				sha256 = [string](Assert-ObjectProperty $previousInstaller 'sha256' 'Previous candidate MSI')
				size = [int64](Assert-ObjectProperty $previousInstaller 'size' 'Previous candidate MSI')
				url = [string](Assert-ObjectProperty $previousInstaller 'url' 'Previous candidate MSI')
			}
		) + $previousRecovery
		foreach ($candidate in $candidates) {
			$tagString = [string](Assert-ObjectProperty $candidate 'immutableTag' 'Previous recovery MSI')
			$fileName = [string](Assert-ObjectProperty $candidate 'fileName' 'Previous recovery MSI')
			$hash = [string](Assert-ObjectProperty $candidate 'sha256' 'Previous recovery MSI')
			$size = [int64](Assert-ObjectProperty $candidate 'size' 'Previous recovery MSI')
			$url = [string](Assert-ObjectProperty $candidate 'url' 'Previous recovery MSI')
			if ($tagString -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' -or
				$fileName -notmatch '^mumble-forked-[A-Za-z0-9._-]+[.]msi$' -or
				$hash -notmatch '^[0-9a-f]{64}$' -or $size -le 0 -or
				$url -cne "https://github.com/$Repository/releases/download/$tagString/$fileName") {
				throw "Previous pointer contains invalid recovery MSI metadata."
			}
			if (-not $knownGood.Contains($tagString) -and $recoveryInstallers.Count -lt 2) {
				$knownGood.Add($tagString)
				$recoveryInstallers.Add([ordered]@{
					immutableTag = $tagString
					fileName = $fileName
					sha256 = $hash
					size = $size
					url = $url
				})
			}
		}
	} elseif ($previousSchema -ne 1) {
		throw "Previous channel pointer has an unsupported schema."
	}
}

if (-not [string]::IsNullOrWhiteSpace($BootstrapRecoverySetPath)) {
	if ($recoveryInstallers.Count -ne 0) {
		throw "Bootstrap recovery metadata cannot be combined with a previous v2 recovery set."
	}
	$bootstrap = Read-ReleaseJson -Path $BootstrapRecoverySetPath
	$bootstrapProperties = @($bootstrap.PSObject.Properties.Name | Sort-Object)
	if (@(Compare-Object -ReferenceObject @('recoveryInstallers', 'schemaVersion') `
		-DifferenceObject $bootstrapProperties).Count -ne 0 -or [int]$bootstrap.schemaVersion -ne 1) {
		throw "Bootstrap recovery set has an invalid root schema."
	}
	$bootstrapEntries = @($bootstrap.recoveryInstallers)
	if ($bootstrapEntries.Count -ne 2) {
		throw "Bootstrap recovery set must contain exactly two MSI records."
	}
	foreach ($candidate in $bootstrapEntries) {
		$properties = @($candidate.PSObject.Properties.Name | Sort-Object)
		if (@(Compare-Object -ReferenceObject @('fileName', 'immutableTag', 'sha256', 'size') `
			-DifferenceObject $properties).Count -ne 0) {
			throw "Bootstrap recovery MSI has an invalid schema."
		}
		$tagString = [string]$candidate.immutableTag
		$fileName = [string]$candidate.fileName
		$hash = [string]$candidate.sha256
		$size = [int64]$candidate.size
		if ($tagString -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' -or
			$fileName -notmatch '^mumble-forked-[A-Za-z0-9._-]+[.]msi$' -or
			$hash -notmatch '^[0-9a-f]{64}$' -or $size -le 0 -or $knownGood.Contains($tagString)) {
			throw "Bootstrap recovery MSI metadata is invalid or duplicated."
		}
		$knownGood.Add($tagString)
		$recoveryInstallers.Add([ordered]@{
			immutableTag = $tagString
			fileName = $fileName
			sha256 = $hash
			size = $size
			url = "https://github.com/$Repository/releases/download/$tagString/$fileName"
		})
	}
}

if ($recoveryInstallers.Count -ne 2) {
	throw "Channel pointer v2 requires exactly two distinct previous recovery MSI files."
}

$artifactName = [string](Assert-ObjectProperty -Object $updatePackage -Name "fileName" -Context "Qualified update package")
if ($artifactName -notmatch '^[A-Za-z0-9._-]+\.mumble-update$' -or
	[string](Assert-ObjectProperty -Object $updatePackage -Name "format" -Context "Qualified update package") -cne "mumble-update-v1") {
	throw "Channel pointer requires a qualified mumble-update-v1 package."
}
$qualifiedInstaller = Assert-ObjectProperty -Object $qualification -Name "installer" -Context "Qualification"
$installerName = [string](Assert-ObjectProperty $qualifiedInstaller 'fileName' 'Qualified installer')
$installerHash = [string](Assert-ObjectProperty $qualifiedInstaller 'sha256' 'Qualified installer')
$installerSize = [int64](Assert-ObjectProperty $qualifiedInstaller 'size' 'Qualified installer')
$candidateExecutableHash = [string](Assert-ObjectProperty $qualifiedInstaller 'executableSha256' 'Qualified installer')
if ($installerName -notmatch '^mumble-forked-[A-Za-z0-9._-]+[.]msi$' -or
	$installerHash -notmatch '^[0-9a-f]{64}$' -or $candidateExecutableHash -notmatch '^[0-9a-f]{64}$' -or
	$installerSize -le 0 -or
	(Assert-ObjectProperty $qualifiedInstaller 'signed' 'Qualified installer') -ne $true) {
	throw "Channel pointer requires an exact signed candidate MSI in qualification evidence."
}
$pointer = [ordered]@{
	schemaVersion       = 2
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
	installer           = [ordered]@{
		fileName = $installerName
		sha256   = $installerHash
		size     = $installerSize
		executableSha256 = $candidateExecutableHash
		url      = "https://github.com/$Repository/releases/download/$immutableTag/$installerName"
	}
	recoveryInstallers  = $recoveryInstallers.ToArray()
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
