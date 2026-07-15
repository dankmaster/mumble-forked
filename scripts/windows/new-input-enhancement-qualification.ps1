[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$SourceRoot,

	[Parameter(Mandatory = $true)]
	[string]$SourceSha,

	[Parameter(Mandatory = $true)]
	[int]$BuildNumber,

	[Parameter(Mandatory = $true)]
	[string]$ArtifactPath,

	[Parameter(Mandatory = $true)]
	[string]$InstallerPath,

	[Parameter(Mandatory = $true)]
	[string]$MsiPayloadVerificationPath,

	[Parameter(Mandatory = $true)]
	[string]$UpdatePackagePath,

	[Parameter(Mandatory = $true)]
	[string]$ModelManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$UnsignedModelManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$UnsignedRecipeManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$ModelManifestSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeManifestSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$Ed25519PublicKeyHex,

	[Parameter(Mandatory = $true)]
	[string]$TestGateResultsPath,

	[Parameter(Mandatory = $true)]
	[string]$SigningResultsPath,

	[Parameter(Mandatory = $true)]
	[string]$MeasuredEvidencePath,

	[string[]]$AllowedUntrackedRoots = @(),

	[string]$OpenSslPath = "",

	[Parameter(Mandatory = $true)]
	[string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
	throw "Source root does not exist: '$SourceRoot'."
}
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path
$expectedSha = Assert-FullGitSha -Sha $SourceSha
$actualSha = (& git -C $sourceRootPath rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0) {
	throw "Unable to resolve Git HEAD under '$sourceRootPath'."
}
$actualSha = (Assert-FullGitSha -Sha ([string]$actualSha) -Context "Checked-out Git SHA")
if ($actualSha -cne $expectedSha) {
	throw "Checked-out Git SHA '$actualSha' does not match requested source SHA '$expectedSha'."
}

$trackedStatus = @(& git -C $sourceRootPath status --porcelain=v1 --untracked-files=normal 2>$null)
if ($LASTEXITCODE -ne 0) {
	throw "Unable to inspect tracked source state under '$sourceRootPath'."
}
$allowedPrefixes = New-Object System.Collections.Generic.List[string]
foreach ($root in $AllowedUntrackedRoots) {
	$normalizedRoot = Assert-SafeRelativeReleasePath -Path $root -Context "Allowed untracked root"
	$allowedPrefixes.Add("?? $($normalizedRoot.TrimEnd('/'))/")
}
$dirtyStatus = @($trackedStatus | Where-Object {
	$entry = ([string]$_).Replace('\', '/')
	if (-not $entry.StartsWith("?? ", [System.StringComparison]::Ordinal)) {
		return $true
	}
	foreach ($prefix in $allowedPrefixes) {
		if ($entry.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
			return $false
		}
	}
	return $true
})
if ($dirtyStatus.Count -gt 0) {
	throw "Qualification requires a clean source tree. Dirty entries: $($dirtyStatus -join '; ')."
}

$testGates = Read-ReleaseJson -Path $TestGateResultsPath
Assert-TestGateResults -GateResults $testGates
$signingResults = Read-ReleaseJson -Path $SigningResultsPath
Assert-SigningResults -SigningResults $signingResults
$measuredEvidence = Read-ReleaseJson -Path $MeasuredEvidencePath
if ([int](Assert-ObjectProperty -Object $measuredEvidence -Name "schemaVersion" -Context "Measured evidence") -ne 1 -or
	(Assert-ObjectProperty -Object $measuredEvidence -Name "passed" -Context "Measured evidence") -ne $true -or
	[string](Assert-ObjectProperty -Object $measuredEvidence -Name "suite" -Context "Measured evidence") -cne "master_quality" -or
	[string](Assert-ObjectProperty -Object $measuredEvidence -Name "sourceSha" -Context "Measured evidence") -cne $expectedSha) {
	throw "Qualification requires passing master_quality evidence for the exact source SHA."
}
$legacyBinarySha256 = [string](Assert-ObjectProperty -Object $measuredEvidence -Name "legacyBinarySha256" -Context "Measured evidence")
if ($legacyBinarySha256 -cnotmatch '^[0-9a-f]{64}$') {
	throw "Measured evidence must identify the exact lowercase legacy executable SHA256."
}
$qualityHarnessSha256 = [string](Assert-ObjectProperty -Object $measuredEvidence -Name "harnessSha256" -Context "Measured evidence")
if ($qualityHarnessSha256 -cnotmatch '^[0-9a-f]{64}$') {
	throw "Measured evidence must identify the exact lowercase protected quality-harness SHA256."
}
$measuredEvidenceItem = Get-Item -LiteralPath $MeasuredEvidencePath -ErrorAction Stop
foreach ($runner in @(Assert-ObjectProperty -Object $measuredEvidence -Name "runners" -Context "Measured evidence")) {
	$runnerClass = [string](Assert-ObjectProperty -Object $runner -Name "runnerClass" -Context "Measured runner evidence")
	$harnessProvenanceSha256 = [string](Assert-ObjectProperty -Object $runner `
		-Name "harnessProvenanceSha256" -Context "Measured runner evidence '$runnerClass'")
	if ($harnessProvenanceSha256 -cnotmatch '^[0-9a-f]{64}$') {
		throw "Measured runner evidence '$runnerClass' has an invalid protected harness-provenance SHA256."
	}
	foreach ($recordName in @("qualityQualification", "originalVoiceQualification")) {
		$record = Assert-ObjectProperty -Object $runner -Name $recordName -Context "Measured runner evidence"
		$fileName = Assert-SafeRelativeReleasePath `
			-Path ([string](Assert-ObjectProperty -Object $record -Name "fileName" -Context "Measured runner evidence")) `
			-Context "Measured runner evidence file"
		if ($fileName.Contains('/')) {
			throw "Measured runner evidence files must be direct siblings of the attestation."
		}
		$filePath = Join-Path $measuredEvidenceItem.DirectoryName $fileName
		if ((Get-ReleaseFileSha256 -Path $filePath) -cne [string]$record.sha256) {
			throw "Measured runner evidence '$fileName' does not match its attested SHA256."
		}
	}
}

$modelManifest = Read-ReleaseJson -Path $ModelManifestPath
$recipeManifest = Read-ReleaseJson -Path $RecipeManifestPath
$unsignedModelManifest = Read-ReleaseJson -Path $UnsignedModelManifestPath
$unsignedRecipeManifest = Read-ReleaseJson -Path $UnsignedRecipeManifestPath
$ed25519PublicKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $Ed25519PublicKeyHex
if (-not (Test-Ed25519DetachedSignature -InputPath $ModelManifestPath `
	-SignaturePath $ModelManifestSignaturePath -PublicKeyHex $ed25519PublicKey -OpenSslPath $OpenSslPath) -or
	-not (Test-Ed25519DetachedSignature -InputPath $RecipeManifestPath `
	-SignaturePath $RecipeManifestSignaturePath -PublicKeyHex $ed25519PublicKey -OpenSslPath $OpenSslPath)) {
	throw "Qualification requires valid detached Ed25519 signatures for both package manifests."
}
$modelCatalogRevision = [string](Assert-ObjectProperty -Object $modelManifest -Name "catalogRevision" -Context "Model manifest")
$recipeCatalogRevision = [string](Assert-ObjectProperty -Object $recipeManifest -Name "catalogRevision" -Context "Recipe manifest")
if ([string]::IsNullOrWhiteSpace($modelCatalogRevision) -or $modelCatalogRevision -cne $recipeCatalogRevision) {
	throw "Packaged model and recipe manifests must have the same non-empty catalogRevision."
}
$unsignedCatalogRevision = [string](Assert-ObjectProperty -Object $unsignedModelManifest -Name "catalogRevision" -Context "Unsigned model manifest")
if ($unsignedCatalogRevision -cne $modelCatalogRevision -or
	[string](Assert-ObjectProperty -Object $unsignedRecipeManifest -Name "catalogRevision" -Context "Unsigned recipe manifest") -cne $modelCatalogRevision -or
	((Assert-ObjectProperty -Object $unsignedRecipeManifest -Name "recipes" -Context "Unsigned recipe manifest") | ConvertTo-Json -Depth 100 -Compress) -cne
	((Assert-ObjectProperty -Object $recipeManifest -Name "recipes" -Context "Packaged recipe manifest") | ConvertTo-Json -Depth 100 -Compress)) {
	throw "Measured unsigned and packaged signed recipe catalogs are not identical."
}
$unsignedModelHash = Get-ReleaseFileSha256 -Path $UnsignedModelManifestPath
if ([string](Assert-ObjectProperty -Object $unsignedRecipeManifest -Name "modelManifestSha256" -Context "Unsigned recipe manifest") -cne $unsignedModelHash) {
	throw "Unsigned recipe manifest does not attest the measured unsigned model manifest."
}
$unsignedModelsById = @{}
foreach ($model in @(Assert-ObjectProperty -Object $unsignedModelManifest -Name "models" -Context "Unsigned model manifest")) {
	$unsignedModelsById[[string]$model.id] = $model
}
foreach ($model in @(Assert-ObjectProperty -Object $modelManifest -Name "models" -Context "Packaged model manifest")) {
	$id = [string]$model.id
	if (-not $unsignedModelsById.ContainsKey($id)) { throw "Packaged model '$id' was not measured before signing." }
	$unsigned = $unsignedModelsById[$id]
	if ([string]$unsigned.path -cne [string]$model.path -or [string]$unsigned.version -cne [string]$model.version) {
		throw "Packaged model '$id' changed identity after measured qualification."
	}
	$extension = [System.IO.Path]::GetExtension([string]$model.path)
	if ($extension -cnotin @('.dll', '.exe') -and [string]$unsigned.sha256 -cne [string]$model.sha256) {
		throw "Non-PE model '$id' changed after measured qualification."
	}
	$unsignedModelsById.Remove($id)
}
if ($unsignedModelsById.Count -ne 0) { throw "Measured unsigned model catalog contains assets missing from the signed package." }

$attestedUnsignedModel = Assert-ObjectProperty -Object $measuredEvidence -Name "unsignedModelManifest" -Context "Measured evidence"
$attestedUnsignedRecipe = Assert-ObjectProperty -Object $measuredEvidence -Name "unsignedRecipeManifest" -Context "Measured evidence"
if ([string]$attestedUnsignedModel.sha256 -cne $unsignedModelHash -or
	[string]$attestedUnsignedRecipe.sha256 -cne (Get-ReleaseFileSha256 -Path $UnsignedRecipeManifestPath) -or
	[string]$measuredEvidence.recipeSetVersion -cne $modelCatalogRevision) {
	throw "Measured evidence is not bound to the unsigned model and recipe manifests that became this package."
}

$artifactItem = Get-Item -LiteralPath $ArtifactPath -ErrorAction Stop
if ($artifactItem.PSIsContainer) {
	throw "Qualified artifact must be a file, not a directory."
}
$installerItem = Get-Item -LiteralPath $InstallerPath -ErrorAction Stop
if ($installerItem.PSIsContainer -or $installerItem.Extension -cne '.msi') {
	throw "Qualified installer must be a signed MSI file."
}
$signedInstallerPath = "installer/$($installerItem.Name)"
$signedInstallerMatches = @($signingResults.files | Where-Object {
	([string]$_.path).Replace('\', '/') -ceq $signedInstallerPath
})
if ($signedInstallerMatches.Count -ne 1 -or
	[string](Assert-ObjectProperty -Object $signedInstallerMatches[0] -Name 'sha256' -Context 'Signed MSI result') -cne
		(Get-ReleaseFileSha256 -Path $installerItem.FullName)) {
	throw "Qualified installer is not byte-identical to the Authenticode-verified MSI result."
}
$artifactExpansion = Join-Path ([IO.Path]::GetTempPath()) ("mumble-qualified-artifact-" + [guid]::NewGuid().ToString('N'))
try {
	Expand-Archive -LiteralPath $artifactItem.FullName -DestinationPath $artifactExpansion
	$nestedInstaller = Join-Path $artifactExpansion ("installer\" + $installerItem.Name)
	if (-not (Test-Path -LiteralPath $nestedInstaller -PathType Leaf) -or
		(Get-ReleaseFileSha256 -Path $nestedInstaller) -cne (Get-ReleaseFileSha256 -Path $installerItem.FullName)) {
		throw "Standalone installer is not byte-identical to the installer in the qualified audit artifact."
	}
	$nestedMsiVerification = Join-Path $artifactExpansion 'metadata\msi-payload-verification.json'
	if (-not (Test-Path -LiteralPath $nestedMsiVerification -PathType Leaf) -or
		(Get-ReleaseFileSha256 -Path $nestedMsiVerification) -cne
			(Get-ReleaseFileSha256 -Path $MsiPayloadVerificationPath)) {
		throw "External MSI payload verification is not byte-identical to the audit-artifact record."
	}
} finally {
	Remove-Item -LiteralPath $artifactExpansion -Recurse -Force -ErrorAction SilentlyContinue
}
$updatePackageItem = Get-Item -LiteralPath $UpdatePackagePath -ErrorAction Stop
if ($updatePackageItem.PSIsContainer -or $updatePackageItem.Extension -cne ".mumble-update") {
	throw "Qualified update package must be a .mumble-update file."
}
& (Join-Path $PSScriptRoot "assert-windows-update-package.ps1") `
	-PackagePath $updatePackageItem.FullName `
	-ExpectedCommit $expectedSha `
	-ExpectedBuild $BuildNumber `
	-ExpectedVersion "1.7.$BuildNumber" `
	-RequireUpdaterRuntime
& (Join-Path $PSScriptRoot 'assert-input-enhancement-msi-payload-verification.ps1') `
	-VerificationPath $MsiPayloadVerificationPath `
	-MsiPath $installerItem.FullName `
	-UpdatePackagePath $updatePackageItem.FullName
$msiPayloadVerification = Read-ReleaseJson -Path $MsiPayloadVerificationPath
$buildId = Get-InputEnhancementBuildId -BuildNumber $BuildNumber -SourceSha $expectedSha
$qualification = [ordered]@{
	schemaVersion = 1
	buildId       = $buildId
	immutableTag  = $buildId
	buildNumber   = $BuildNumber
	source        = [ordered]@{
		sha   = $expectedSha
		dirty = $false
	}
	artifact      = [ordered]@{
		fileName = $artifactItem.Name
		sha256   = Get-ReleaseFileSha256 -Path $artifactItem.FullName
		size     = [int64]$artifactItem.Length
		signed   = $true
	}
	installer     = [ordered]@{
		fileName = $installerItem.Name
		sha256   = Get-ReleaseFileSha256 -Path $installerItem.FullName
		size     = [int64]$installerItem.Length
		signed   = $true
		payloadVerification = [ordered]@{
			fileName = (Get-Item -LiteralPath $MsiPayloadVerificationPath).Name
			sha256 = Get-ReleaseFileSha256 -Path $MsiPayloadVerificationPath
			passed = $true
			verifiedFileCount = [int]$msiPayloadVerification.verifiedFileCount
		}
	}
	updatePackage = [ordered]@{
		fileName = $updatePackageItem.Name
		sha256   = Get-ReleaseFileSha256 -Path $updatePackageItem.FullName
		size     = [int64]$updatePackageItem.Length
		format   = "mumble-update-v1"
	}
	modelManifest = [ordered]@{
		fileName       = (Get-Item -LiteralPath $ModelManifestPath).Name
		sha256         = Get-ReleaseFileSha256 -Path $ModelManifestPath
		catalogRevision = $modelCatalogRevision
	}
	recipeManifest = [ordered]@{
		fileName       = (Get-Item -LiteralPath $RecipeManifestPath).Name
		sha256         = Get-ReleaseFileSha256 -Path $RecipeManifestPath
		catalogRevision = $recipeCatalogRevision
	}
	unsignedModelManifest = [ordered]@{
		fileName       = (Get-Item -LiteralPath $UnsignedModelManifestPath).Name
		sha256         = $unsignedModelHash
		catalogRevision = $unsignedCatalogRevision
	}
	unsignedRecipeManifest = [ordered]@{
		fileName       = (Get-Item -LiteralPath $UnsignedRecipeManifestPath).Name
		sha256         = Get-ReleaseFileSha256 -Path $UnsignedRecipeManifestPath
		catalogRevision = $unsignedCatalogRevision
	}
	detachedSigning = [ordered]@{
		algorithm    = "Ed25519"
		encoding     = "raw"
		verified     = $true
		publicKeyHex = $ed25519PublicKey
		modelManifestSignature = [ordered]@{
			fileName = (Get-Item -LiteralPath $ModelManifestSignaturePath).Name
			sha256   = Get-ReleaseFileSha256 -Path $ModelManifestSignaturePath
		}
		recipeManifestSignature = [ordered]@{
			fileName = (Get-Item -LiteralPath $RecipeManifestSignaturePath).Name
			sha256   = Get-ReleaseFileSha256 -Path $RecipeManifestSignaturePath
		}
	}
	testGates      = [ordered]@{
		fileName = (Get-Item -LiteralPath $TestGateResultsPath).Name
		sha256   = Get-ReleaseFileSha256 -Path $TestGateResultsPath
		passed   = $true
		gates    = @($testGates.gates)
	}
	measuredQuality = [ordered]@{
		fileName            = $measuredEvidenceItem.Name
		sha256              = Get-ReleaseFileSha256 -Path $measuredEvidenceItem.FullName
		passed              = $true
		suite               = "master_quality"
		sourceSha           = $expectedSha
		testedBinarySha256  = [string]$measuredEvidence.testedBinarySha256
		legacyBinarySha256  = $legacyBinarySha256
		corpusLockSha256    = [string]$measuredEvidence.corpusLockSha256
		harnessSha256       = $qualityHarnessSha256
		runners             = @($measuredEvidence.runners)
	}
	signing        = [ordered]@{
		required              = $true
		verified              = $true
		expectedSignerSubject = [string]$signingResults.expectedSignerSubject
		resultsFileName       = (Get-Item -LiteralPath $SigningResultsPath).Name
		resultsSha256         = Get-ReleaseFileSha256 -Path $SigningResultsPath
	}
	createdAtUtc   = (Get-Date).ToUniversalTime().ToString("o")
}

Write-ReleaseJson -Value $qualification -Path $OutputPath
$qualificationHash = Get-ReleaseFileSha256 -Path $OutputPath
$hashLine = "$qualificationHash  $((Get-Item -LiteralPath $OutputPath).Name)`n"
[System.IO.File]::WriteAllText("$OutputPath.sha256", $hashLine, [System.Text.UTF8Encoding]::new($false))

Write-Host "Created qualification '$OutputPath' for immutable build '$buildId'."
