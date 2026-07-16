[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet("preview", "stable")]
	[string]$Channel,

	[Parameter(Mandatory = $true)]
	[string]$Repository,

	[Parameter(Mandatory = $true)]
	[string]$PointerPath,

	[Parameter(Mandatory = $true)]
	[string]$PointerSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$PublicKeyHex,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedSignerSubject,

	[Parameter(Mandatory = $true)]
	[string]$OutputRoot,

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
	throw "Repository must use the GitHub owner/name form."
}
if ([string]::IsNullOrWhiteSpace($ExpectedSignerSubject)) {
	throw "ExpectedSignerSubject is required for recovery validation."
}
$null = Assert-Ed25519PublicKeyHex -PublicKeyHex $PublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $PointerPath -SignaturePath $PointerSignaturePath `
	-PublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath)) {
	throw "Previous channel pointer has no valid detached Ed25519 signature."
}

$pointer = Read-ReleaseJson -Path $PointerPath
if ([int](Assert-ObjectProperty $pointer 'schemaVersion' 'Previous channel pointer') -ne 2 -or
	[string](Assert-ObjectProperty $pointer 'channel' 'Previous channel pointer') -cne $Channel) {
	throw "Previous channel pointer schema or channel is invalid."
}
$immutableTag = [string](Assert-ObjectProperty $pointer 'immutableTag' 'Previous channel pointer')
$sourceSha = Assert-FullGitSha -Sha ([string](Assert-ObjectProperty $pointer 'sourceSha' 'Previous channel pointer')) `
	-Context 'Previous channel pointer source SHA'
$buildNumber = [int](Assert-ObjectProperty $pointer 'buildNumber' 'Previous channel pointer')
if ($immutableTag -cne (Get-InputEnhancementBuildId -BuildNumber $buildNumber -SourceSha $sourceSha) -or
	[string](Assert-ObjectProperty $pointer 'buildId' 'Previous channel pointer') -cne $immutableTag) {
	throw "Previous channel pointer immutable identity is inconsistent."
}
$knownGoodTags = @(Assert-ObjectProperty $pointer 'knownGoodTags' 'Previous channel pointer')
if ($knownGoodTags.Count -ne 3 -or
	[string]$knownGoodTags[0] -cne $immutableTag -or
	@($knownGoodTags | Select-Object -Unique).Count -ne $knownGoodTags.Count) {
	throw "Previous channel pointer does not lead with its current immutable build."
}

$artifact = Assert-ObjectProperty $pointer 'artifact' 'Previous channel pointer'
$candidateInstaller = Assert-ObjectProperty $pointer 'installer' 'Previous channel pointer'
$recoveryInstallers = @(Assert-ObjectProperty $pointer 'recoveryInstallers' 'Previous channel pointer')
$qualification = Assert-ObjectProperty $pointer 'qualification' 'Previous channel pointer'
$releaseSmoke = Assert-ObjectProperty $pointer 'releaseSmoke' 'Previous channel pointer'
$artifactName = [string](Assert-ObjectProperty $artifact 'fileName' 'Previous recovery artifact')
$artifactHash = [string](Assert-ObjectProperty $artifact 'sha256' 'Previous recovery artifact')
$artifactSize = [int64](Assert-ObjectProperty $artifact 'size' 'Previous recovery artifact')
$baseUrl = "https://github.com/$Repository/releases/download/$immutableTag"
if ($artifactName -notmatch '^[A-Za-z0-9._-]+[.]mumble-update$' -or $artifactHash -notmatch '^[0-9a-f]{64}$' -or
	$artifactSize -le 0 -or [string](Assert-ObjectProperty $artifact 'url' 'Previous recovery artifact') -cne "$baseUrl/$artifactName" -or
	[string](Assert-ObjectProperty $qualification 'sha256' 'Previous qualification') -notmatch '^[0-9a-f]{64}$' -or
	[string](Assert-ObjectProperty $qualification 'url' 'Previous qualification') -cne "$baseUrl/qualification.json" -or
	[string](Assert-ObjectProperty $releaseSmoke 'sha256' 'Previous release smoke') -notmatch '^[0-9a-f]{64}$' -or
	[string](Assert-ObjectProperty $releaseSmoke 'url' 'Previous release smoke') -cne "$baseUrl/release-smoke.json") {
	throw "Previous channel pointer recovery references are invalid."
}
$candidateInstallerName = [string](Assert-ObjectProperty $candidateInstaller 'fileName' 'Previous candidate MSI')
$candidateInstallerHash = [string](Assert-ObjectProperty $candidateInstaller 'sha256' 'Previous candidate MSI')
$candidateInstallerSize = [int64](Assert-ObjectProperty $candidateInstaller 'size' 'Previous candidate MSI')
$candidateExecutableHash = [string](Assert-ObjectProperty $candidateInstaller 'executableSha256' 'Previous candidate MSI')
if ($candidateInstallerName -notmatch '^mumble-forked-[A-Za-z0-9._-]+[.]msi$' -or
	$candidateInstallerHash -notmatch '^[0-9a-f]{64}$' -or
	$candidateExecutableHash -notmatch '^[0-9a-f]{64}$' -or $candidateInstallerSize -le 0 -or
	[string](Assert-ObjectProperty $candidateInstaller 'url' 'Previous candidate MSI') -cne
	"$baseUrl/$candidateInstallerName" -or $recoveryInstallers.Count -ne 2) {
	throw "Previous channel pointer candidate/recovery MSI set is invalid."
}
$seenRecoveryTags = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
for ($index = 0; $index -lt $recoveryInstallers.Count; $index++) {
	$recovery = $recoveryInstallers[$index]
	$tag = [string](Assert-ObjectProperty $recovery 'immutableTag' 'Previous recovery MSI')
	$name = [string](Assert-ObjectProperty $recovery 'fileName' 'Previous recovery MSI')
	$hash = [string](Assert-ObjectProperty $recovery 'sha256' 'Previous recovery MSI')
	$size = [int64](Assert-ObjectProperty $recovery 'size' 'Previous recovery MSI')
	$url = [string](Assert-ObjectProperty $recovery 'url' 'Previous recovery MSI')
	if ($tag -notmatch '^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$' -or
		-not $seenRecoveryTags.Add($tag) -or [string]$knownGoodTags[$index + 1] -cne $tag -or
		$name -notmatch '^mumble-forked-[A-Za-z0-9._-]+[.]msi$' -or
		$hash -notmatch '^[0-9a-f]{64}$' -or $size -le 0 -or
		$url -cne "https://github.com/$Repository/releases/download/$tag/$name") {
		throw "Previous channel pointer recovery MSI entry $index is invalid."
	}
}

$remoteTag = @(git ls-remote origin "refs/tags/$immutableTag")
if ($LASTEXITCODE -ne 0 -or $remoteTag.Count -ne 1 -or
	(([string]$remoteTag[0] -split '\s+')[0].ToLowerInvariant()) -cne $sourceSha) {
	throw "Previous immutable recovery tag is missing or changed."
}

if (Test-Path -LiteralPath $OutputRoot) {
	Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
foreach ($assetName in @($artifactName, 'qualification.json', 'qualification.json.sig', 'release-smoke.json', 'release-smoke.json.sig')) {
	gh release download $immutableTag --repo $Repository --pattern $assetName --dir $OutputRoot
	if ($LASTEXITCODE -ne 0) { throw "Recovery asset '$assetName' is missing from '$immutableTag'." }
}
$downloadedFiles = @(Get-ChildItem -LiteralPath $OutputRoot -File)
if ($downloadedFiles.Count -ne 5) {
	throw "Recovery release did not yield exactly the five required recovery assets."
}
$artifactPath = Join-Path $OutputRoot $artifactName
$qualificationPath = Join-Path $OutputRoot 'qualification.json'
$releaseSmokePath = Join-Path $OutputRoot 'release-smoke.json'
if ((Get-Item -LiteralPath $artifactPath).Length -ne $artifactSize -or
	(Get-ReleaseFileSha256 -Path $artifactPath) -cne $artifactHash -or
	(Get-ReleaseFileSha256 -Path $qualificationPath) -cne [string]$qualification.sha256 -or
	(Get-ReleaseFileSha256 -Path $releaseSmokePath) -cne [string]$releaseSmoke.sha256) {
	throw "Downloaded recovery assets do not match the previous signed pointer."
}
foreach ($jsonName in @('qualification.json', 'release-smoke.json')) {
	$jsonPath = Join-Path $OutputRoot $jsonName
	$signaturePath = "$jsonPath.sig"
	if (-not (Test-Ed25519DetachedSignature -InputPath $jsonPath -SignaturePath $signaturePath `
		-PublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath)) {
		throw "Recovery evidence '$jsonName' has no valid detached signature."
	}
}

$qualified = Read-ReleaseJson -Path $qualificationPath
if ([string](Assert-ObjectProperty $qualified 'buildId' 'Recovery qualification') -cne $immutableTag -or
	[string](Assert-ObjectProperty (Assert-ObjectProperty $qualified 'source' 'Recovery qualification') `
		'sha' 'Recovery qualification source') -cne $sourceSha) {
	throw "Recovery qualification does not attest the previous pointer identity."
}
$qualifiedInstaller = Assert-ObjectProperty $qualified 'installer' 'Recovery qualification'
$installerName = [string](Assert-ObjectProperty $qualifiedInstaller 'fileName' 'Recovery installer')
$installerHash = [string](Assert-ObjectProperty $qualifiedInstaller 'sha256' 'Recovery installer')
$installerSize = [int64](Assert-ObjectProperty $qualifiedInstaller 'size' 'Recovery installer')
$qualifiedExecutableHash = [string](Assert-ObjectProperty $qualifiedInstaller 'executableSha256' 'Recovery installer')
if ($installerName -notmatch '^mumble-forked-[A-Za-z0-9._-]+[.]msi$' -or
	$installerHash -notmatch '^[0-9a-f]{64}$' -or $qualifiedExecutableHash -notmatch '^[0-9a-f]{64}$' -or
	$installerSize -le 0 -or
	(Assert-ObjectProperty $qualifiedInstaller 'signed' 'Recovery installer') -ne $true) {
	throw "Recovery qualification contains invalid signed-installer metadata."
}
gh release download $immutableTag --repo $Repository --pattern $installerName --dir $OutputRoot
if ($LASTEXITCODE -ne 0) { throw "Recovery installer '$installerName' is missing from '$immutableTag'." }
$installerPath = Join-Path $OutputRoot $installerName
if ((Get-Item -LiteralPath $installerPath).Length -ne $installerSize -or
	(Get-ReleaseFileSha256 -Path $installerPath) -cne $installerHash) {
	throw "Downloaded recovery installer does not match the signed qualification."
}
if ($installerName -cne $candidateInstallerName -or $installerHash -cne $candidateInstallerHash -or
	$installerSize -ne $candidateInstallerSize -or $qualifiedExecutableHash -cne $candidateExecutableHash) {
	throw "Previous qualification and channel pointer disagree about the candidate MSI."
}
foreach ($recovery in $recoveryInstallers) {
	$tag = [string]$recovery.immutableTag
	$name = [string]$recovery.fileName
	gh release download $tag --repo $Repository --pattern $name --dir $OutputRoot
	if ($LASTEXITCODE -ne 0) { throw "Recovery installer '$name' is missing from '$tag'." }
	$path = Join-Path $OutputRoot $name
	if ((Get-Item -LiteralPath $path).Length -ne [int64]$recovery.size -or
		(Get-ReleaseFileSha256 -Path $path) -cne [string]$recovery.sha256) {
		throw "Downloaded recovery installer '$name' does not match the signed channel pointer."
	}
}
& (Join-Path $PSScriptRoot 'assert-input-enhancement-signatures.ps1') `
	-Root $OutputRoot -ExpectedSignerSubject $ExpectedSignerSubject `
	-OutputPath (Join-Path $OutputRoot 'installer-authenticode-verification.json')
$smoke = Read-ReleaseJson -Path $releaseSmokePath
if ([string](Assert-ObjectProperty $smoke 'buildId' 'Recovery release smoke') -cne $immutableTag -or
	[string](Assert-ObjectProperty $smoke 'sourceSha' 'Recovery release smoke') -cne $sourceSha -or
	(Assert-ObjectProperty $smoke 'passed' 'Recovery release smoke') -ne $true) {
	throw "Recovery release-smoke evidence does not attest a passing previous build."
}

$expandedRoot = Join-Path $OutputRoot 'expanded-update'
& (Join-Path $PSScriptRoot 'assert-windows-update-package.ps1') `
	-PackagePath $artifactPath -ExpectedCommit $sourceSha -ExpectedBuild $buildNumber `
	-ExpectedVersion "1.7.$buildNumber" -RequireUpdaterRuntime -RequireGStreamerRuntime `
	-ExpandedPayloadPath $expandedRoot
& (Join-Path $PSScriptRoot 'assert-input-enhancement-package-manifests.ps1') -StageRoot $expandedRoot
& (Join-Path $PSScriptRoot 'assert-input-enhancement-package-signatures.ps1') `
	-StageRoot $expandedRoot -PublicKeyHex $PublicKeyHex -OpenSslPath $OpenSslPath
& (Join-Path $PSScriptRoot 'assert-input-enhancement-signatures.ps1') `
	-Root $expandedRoot -ExpectedSignerSubject $ExpectedSignerSubject `
	-OutputPath (Join-Path $OutputRoot 'authenticode-verification.json')
& (Join-Path $PSScriptRoot 'assert-input-enhancement-msi-payload.ps1') `
	-MsiPath $installerPath -QualifiedPayloadRoot $expandedRoot `
	-OutputPath (Join-Path $OutputRoot 'msi-payload-verification.json')

Write-Host "Verified complete recovery target '$immutableTag' from the previous signed $Channel pointer."
