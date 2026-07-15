[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$QualificationPath,

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
	[string]$ExpectedEd25519PublicKeyHex,

	[Parameter(Mandatory = $true)]
	[string]$TestGateResultsPath,

	[Parameter(Mandatory = $true)]
	[string]$SigningResultsPath,

	[Parameter(Mandatory = $true)]
	[string]$MeasuredEvidencePath,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedSourceSha,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedBuildId,

	[string]$ExpectedSignerSubject = "",

	[string]$OpenSslPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$qualification = Read-ReleaseJson -Path $QualificationPath
if ([int](Assert-ObjectProperty -Object $qualification -Name "schemaVersion" -Context "Qualification") -ne 1) {
	throw "Unsupported qualification schema version."
}

$source = Assert-ObjectProperty -Object $qualification -Name "source" -Context "Qualification"
$sourceSha = Assert-FullGitSha -Sha ([string](Assert-ObjectProperty -Object $source -Name "sha" -Context "Qualification source")) -Context "Qualification source SHA"
$expectedSha = Assert-FullGitSha -Sha $ExpectedSourceSha -Context "Expected source SHA"
if ($sourceSha -cne $expectedSha) {
	throw "Qualification source SHA '$sourceSha' does not match expected '$expectedSha'."
}
if ((Assert-ObjectProperty -Object $source -Name "dirty" -Context "Qualification source") -ne $false) {
	throw "Qualification source must explicitly report dirty=false."
}

$buildNumber = [int](Assert-ObjectProperty -Object $qualification -Name "buildNumber" -Context "Qualification")
$derivedBuildId = Get-InputEnhancementBuildId -BuildNumber $buildNumber -SourceSha $sourceSha
$reportedBuildId = [string](Assert-ObjectProperty -Object $qualification -Name "buildId" -Context "Qualification")
$immutableTag = [string](Assert-ObjectProperty -Object $qualification -Name "immutableTag" -Context "Qualification")
if ($reportedBuildId -cne $derivedBuildId -or $reportedBuildId -cne $ExpectedBuildId -or $immutableTag -cne $reportedBuildId) {
	throw "Qualification build ID/tag mismatch. Reported '$reportedBuildId', derived '$derivedBuildId', expected '$ExpectedBuildId', tag '$immutableTag'."
}

function Assert-QualifiedFile {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Record,
		[Parameter(Mandatory = $true)]
		[string]$Path,
		[Parameter(Mandatory = $true)]
		[string]$Label,
		[string]$NameProperty = "fileName",
		[string]$HashProperty = "sha256"
	)

	$file = Get-Item -LiteralPath $Path -ErrorAction Stop
	if ($file.PSIsContainer) {
		throw "$Label path must be a file."
	}
	$reportedName = [string](Assert-ObjectProperty -Object $Record -Name $NameProperty -Context $Label)
	if ($reportedName -cne $file.Name) {
		throw "$Label name '$reportedName' does not match downloaded file '$($file.Name)'."
	}
	$reportedHash = [string](Assert-ObjectProperty -Object $Record -Name $HashProperty -Context $Label)
	$actualHash = Get-ReleaseFileSha256 -Path $file.FullName
	if ($reportedHash -cne $actualHash) {
		throw "$Label SHA256 mismatch. Reported '$reportedHash', actual '$actualHash'."
	}
	return $file
}

$artifactRecord = Assert-ObjectProperty -Object $qualification -Name "artifact" -Context "Qualification"
$artifactFile = Assert-QualifiedFile -Record $artifactRecord -Path $ArtifactPath -Label "Qualified artifact"
if ([int64](Assert-ObjectProperty -Object $artifactRecord -Name "size" -Context "Qualified artifact") -ne [int64]$artifactFile.Length) {
	throw "Qualified artifact size does not match qualification."
}
if ((Assert-ObjectProperty -Object $artifactRecord -Name "signed" -Context "Qualified artifact") -ne $true) {
	throw "Qualified artifact is not marked signed."
}
$installerRecord = Assert-ObjectProperty -Object $qualification -Name "installer" -Context "Qualification"
$installerFile = Assert-QualifiedFile -Record $installerRecord -Path $InstallerPath -Label "Qualified installer"
if ($installerFile.Extension -cne '.msi' -or
	[int64](Assert-ObjectProperty -Object $installerRecord -Name "size" -Context "Qualified installer") -ne [int64]$installerFile.Length -or
	(Assert-ObjectProperty -Object $installerRecord -Name "signed" -Context "Qualified installer") -ne $true) {
	throw "Qualified installer metadata is invalid."
}
$artifactExpansion = Join-Path ([IO.Path]::GetTempPath()) ("mumble-qualified-artifact-" + [guid]::NewGuid().ToString('N'))
try {
	Expand-Archive -LiteralPath $artifactFile.FullName -DestinationPath $artifactExpansion
	$nestedInstaller = Join-Path $artifactExpansion ("installer\" + $installerFile.Name)
	if (-not (Test-Path -LiteralPath $nestedInstaller -PathType Leaf) -or
		(Get-ReleaseFileSha256 -Path $nestedInstaller) -cne (Get-ReleaseFileSha256 -Path $installerFile.FullName)) {
		throw "Standalone installer does not match the qualified audit artifact."
	}
	$nestedMsiVerification = Join-Path $artifactExpansion 'metadata\msi-payload-verification.json'
	if (-not (Test-Path -LiteralPath $nestedMsiVerification -PathType Leaf) -or
		(Get-ReleaseFileSha256 -Path $nestedMsiVerification) -cne
			(Get-ReleaseFileSha256 -Path $MsiPayloadVerificationPath)) {
		throw "MSI payload verification does not match the qualified audit artifact."
	}
} finally {
	Remove-Item -LiteralPath $artifactExpansion -Recurse -Force -ErrorAction SilentlyContinue
}
$updatePackageRecord = Assert-ObjectProperty -Object $qualification -Name "updatePackage" -Context "Qualification"
$updatePackageFile = Assert-QualifiedFile -Record $updatePackageRecord -Path $UpdatePackagePath -Label "Qualified update package"
if ($updatePackageFile.Extension -cne ".mumble-update" -or
	[string](Assert-ObjectProperty -Object $updatePackageRecord -Name "format" -Context "Qualified update package") -cne "mumble-update-v1" -or
	[int64](Assert-ObjectProperty -Object $updatePackageRecord -Name "size" -Context "Qualified update package") -ne [int64]$updatePackageFile.Length) {
	throw "Qualified update package metadata is invalid."
}
& (Join-Path $PSScriptRoot "assert-windows-update-package.ps1") `
	-PackagePath $updatePackageFile.FullName `
	-ExpectedCommit $sourceSha `
	-ExpectedBuild $buildNumber `
	-ExpectedVersion "1.7.$buildNumber" `
	-RequireUpdaterRuntime
& (Join-Path $PSScriptRoot 'assert-input-enhancement-msi-payload-verification.ps1') `
	-VerificationPath $MsiPayloadVerificationPath `
	-MsiPath $installerFile.FullName `
	-UpdatePackagePath $updatePackageFile.FullName
$msiPayloadVerification = Read-ReleaseJson -Path $MsiPayloadVerificationPath
$msiPayloadVerificationRecord = Assert-ObjectProperty -Object $installerRecord `
	-Name 'payloadVerification' -Context 'Qualified installer'
$null = Assert-QualifiedFile -Record $msiPayloadVerificationRecord `
	-Path $MsiPayloadVerificationPath -Label 'MSI payload verification'
if ((Assert-ObjectProperty -Object $msiPayloadVerificationRecord -Name 'passed' `
	-Context 'MSI payload verification') -ne $true -or
	[int](Assert-ObjectProperty -Object $msiPayloadVerificationRecord -Name 'verifiedFileCount' `
		-Context 'MSI payload verification') -ne [int]$msiPayloadVerification.verifiedFileCount) {
	throw "Qualification does not attest the complete passing MSI payload verification."
}

$modelRecord = Assert-ObjectProperty -Object $qualification -Name "modelManifest" -Context "Qualification"
$recipeRecord = Assert-ObjectProperty -Object $qualification -Name "recipeManifest" -Context "Qualification"
$unsignedModelRecord = Assert-ObjectProperty -Object $qualification -Name "unsignedModelManifest" -Context "Qualification"
$unsignedRecipeRecord = Assert-ObjectProperty -Object $qualification -Name "unsignedRecipeManifest" -Context "Qualification"
$null = Assert-QualifiedFile -Record $modelRecord -Path $ModelManifestPath -Label "Model manifest"
$null = Assert-QualifiedFile -Record $recipeRecord -Path $RecipeManifestPath -Label "Recipe manifest"
$null = Assert-QualifiedFile -Record $unsignedModelRecord -Path $UnsignedModelManifestPath -Label "Unsigned model manifest"
$null = Assert-QualifiedFile -Record $unsignedRecipeRecord -Path $UnsignedRecipeManifestPath -Label "Unsigned recipe manifest"
$modelManifest = Read-ReleaseJson -Path $ModelManifestPath
$recipeManifest = Read-ReleaseJson -Path $RecipeManifestPath
$unsignedModelManifest = Read-ReleaseJson -Path $UnsignedModelManifestPath
$unsignedRecipeManifest = Read-ReleaseJson -Path $UnsignedRecipeManifestPath
$catalogRevision = [string](Assert-ObjectProperty -Object $modelRecord -Name "catalogRevision" -Context "Qualified model manifest")
if ($catalogRevision -cne [string]$modelManifest.catalogRevision -or
	$catalogRevision -cne [string]$recipeManifest.catalogRevision -or
	$catalogRevision -cne [string]$recipeRecord.catalogRevision) {
	throw "Qualified model/recipe catalog revisions do not match."
}
if ($catalogRevision -cne [string]$unsignedModelManifest.catalogRevision -or
	$catalogRevision -cne [string]$unsignedRecipeManifest.catalogRevision -or
	((Assert-ObjectProperty -Object $unsignedRecipeManifest -Name "recipes" -Context "Unsigned recipe manifest") | ConvertTo-Json -Depth 100 -Compress) -cne
	((Assert-ObjectProperty -Object $recipeManifest -Name "recipes" -Context "Packaged recipe manifest") | ConvertTo-Json -Depth 100 -Compress) -or
	[string]$unsignedRecipeManifest.modelManifestSha256 -cne (Get-ReleaseFileSha256 -Path $UnsignedModelManifestPath)) {
	throw "Unsigned measured manifests do not identify the exact signed package catalog."
}
$unsignedModels = @{}
foreach ($model in @($unsignedModelManifest.models)) { $unsignedModels[[string]$model.id] = $model }
foreach ($model in @($modelManifest.models)) {
	$id = [string]$model.id
	if (-not $unsignedModels.ContainsKey($id) -or [string]$unsignedModels[$id].path -cne [string]$model.path -or
		[string]$unsignedModels[$id].version -cne [string]$model.version) {
		throw "Packaged model '$id' was not present with the same identity in measured evidence."
	}
	if ([IO.Path]::GetExtension([string]$model.path) -cnotin @('.dll', '.exe') -and
		[string]$unsignedModels[$id].sha256 -cne [string]$model.sha256) {
		throw "Non-PE model '$id' changed after quality measurement."
	}
	$unsignedModels.Remove($id)
}
if ($unsignedModels.Count -ne 0) { throw "Measured model assets are missing from the signed package." }

$detachedSigning = Assert-ObjectProperty -Object $qualification -Name "detachedSigning" -Context "Qualification"
if ([string](Assert-ObjectProperty -Object $detachedSigning -Name "algorithm" -Context "Qualified detached signing") -cne "Ed25519" -or
	[string](Assert-ObjectProperty -Object $detachedSigning -Name "encoding" -Context "Qualified detached signing") -cne "raw" -or
	(Assert-ObjectProperty -Object $detachedSigning -Name "verified" -Context "Qualified detached signing") -ne $true) {
	throw "Qualification detached signing is not a verified raw Ed25519 record."
}
$qualifiedPublicKey = Assert-Ed25519PublicKeyHex `
	-PublicKeyHex ([string](Assert-ObjectProperty -Object $detachedSigning -Name "publicKeyHex" -Context "Qualified detached signing")) `
	-Context "Qualified detached-signing public key"
$expectedPublicKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $ExpectedEd25519PublicKeyHex `
	-Context "Expected detached-signing public key"
if ($qualifiedPublicKey -cne $expectedPublicKey) {
	throw "Qualification detached-signing public key does not match the protected release environment."
}
$modelSignatureRecord = Assert-ObjectProperty -Object $detachedSigning -Name "modelManifestSignature" -Context "Qualified detached signing"
$recipeSignatureRecord = Assert-ObjectProperty -Object $detachedSigning -Name "recipeManifestSignature" -Context "Qualified detached signing"
$null = Assert-QualifiedFile -Record $modelSignatureRecord -Path $ModelManifestSignaturePath -Label "Model manifest signature"
$null = Assert-QualifiedFile -Record $recipeSignatureRecord -Path $RecipeManifestSignaturePath -Label "Recipe manifest signature"
if (-not (Test-Ed25519DetachedSignature -InputPath $ModelManifestPath `
	-SignaturePath $ModelManifestSignaturePath -PublicKeyHex $qualifiedPublicKey -OpenSslPath $OpenSslPath) -or
	-not (Test-Ed25519DetachedSignature -InputPath $RecipeManifestPath `
	-SignaturePath $RecipeManifestSignaturePath -PublicKeyHex $qualifiedPublicKey -OpenSslPath $OpenSslPath)) {
	throw "A qualified package-manifest Ed25519 signature failed cryptographic verification."
}
& (Join-Path $PSScriptRoot "assert-input-enhancement-package-manifest-binding.ps1") `
	-UpdatePackagePath $updatePackageFile.FullName `
	-ModelManifestPath $ModelManifestPath `
	-RecipeManifestPath $RecipeManifestPath `
	-ModelManifestSignaturePath $ModelManifestSignaturePath `
	-RecipeManifestSignaturePath $RecipeManifestSignaturePath

$testRecord = Assert-ObjectProperty -Object $qualification -Name "testGates" -Context "Qualification"
$null = Assert-QualifiedFile -Record $testRecord -Path $TestGateResultsPath -Label "Test gate results"
$testGates = Read-ReleaseJson -Path $TestGateResultsPath
Assert-TestGateResults -GateResults $testGates
if ((Assert-ObjectProperty -Object $testRecord -Name "passed" -Context "Qualified test gates") -ne $true) {
	throw "Qualification does not mark test gates passed."
}

$measuredRecord = Assert-ObjectProperty -Object $qualification -Name "measuredQuality" -Context "Qualification"
$null = Assert-QualifiedFile -Record $measuredRecord -Path $MeasuredEvidencePath -Label "Measured quality evidence"
$measuredEvidence = Read-ReleaseJson -Path $MeasuredEvidencePath
if ([int](Assert-ObjectProperty -Object $measuredEvidence -Name "schemaVersion" -Context "Measured evidence") -ne 1 -or
	(Assert-ObjectProperty -Object $measuredEvidence -Name "passed" -Context "Measured evidence") -ne $true -or
	[string](Assert-ObjectProperty -Object $measuredEvidence -Name "suite" -Context "Measured evidence") -cne "master_quality" -or
	[string](Assert-ObjectProperty -Object $measuredEvidence -Name "sourceSha" -Context "Measured evidence") -cne $sourceSha -or
	[string](Assert-ObjectProperty -Object $measuredRecord -Name "testedBinarySha256" -Context "Qualified measured evidence") -cne [string]$measuredEvidence.testedBinarySha256 -or
	[string](Assert-ObjectProperty -Object $measuredRecord -Name "legacyBinarySha256" -Context "Qualified measured evidence") -cne [string](Assert-ObjectProperty -Object $measuredEvidence -Name "legacyBinarySha256" -Context "Measured evidence") -or
	[string](Assert-ObjectProperty -Object $measuredRecord -Name "corpusLockSha256" -Context "Qualified measured evidence") -cne [string]$measuredEvidence.corpusLockSha256 -or
	[string](Assert-ObjectProperty -Object $measuredRecord -Name "harnessSha256" -Context "Qualified measured evidence") -cne [string](Assert-ObjectProperty -Object $measuredEvidence -Name "harnessSha256" -Context "Measured evidence") -or
	[string]$measuredEvidence.harnessSha256 -cnotmatch '^[0-9a-f]{64}$') {
	throw "Qualification measured-quality evidence is invalid or belongs to another source/binary/corpus."
}
$attestedUnsignedModel = Assert-ObjectProperty -Object $measuredEvidence -Name "unsignedModelManifest" -Context "Measured evidence"
$attestedUnsignedRecipe = Assert-ObjectProperty -Object $measuredEvidence -Name "unsignedRecipeManifest" -Context "Measured evidence"
if ([string]$attestedUnsignedModel.sha256 -cne (Get-ReleaseFileSha256 -Path $UnsignedModelManifestPath) -or
	[string]$attestedUnsignedRecipe.sha256 -cne (Get-ReleaseFileSha256 -Path $UnsignedRecipeManifestPath) -or
	[string]$measuredEvidence.recipeSetVersion -cne $catalogRevision) {
	throw "Measured quality is not bound to the unsigned package manifests."
}
$measuredEvidenceItem = Get-Item -LiteralPath $MeasuredEvidencePath -ErrorAction Stop
$runnerClasses = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::Ordinal)
foreach ($runner in @(Assert-ObjectProperty -Object $measuredEvidence -Name "runners" -Context "Measured evidence")) {
	$runnerClass = [string](Assert-ObjectProperty -Object $runner -Name "runnerClass" -Context "Measured runner evidence")
	$null = $runnerClasses.Add($runnerClass)
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
		$evidence = Read-ReleaseJson -Path $filePath
		if ($recordName -ceq "qualityQualification") {
			$build = Assert-ObjectProperty -Object $evidence -Name "build" -Context "Measured quality '$runnerClass'"
			$coverage = Assert-ObjectProperty -Object $evidence -Name "coverage" -Context "Measured quality '$runnerClass'"
			$reportedModels = @((Assert-ObjectProperty -Object $build -Name "model_hashes" -Context "Measured quality '$runnerClass'") | ForEach-Object { [string]$_ } | Sort-Object)
			$attestedModels = @($measuredEvidence.modelHashes | ForEach-Object { [string]$_ } | Sort-Object)
			if ([string]$evidence.suite -cne "master_quality" -or [string]$evidence.status -cne "passed" -or
				[string]$build.git_sha -cne $sourceSha -or [string]$build.tested_binary_sha256 -cne [string]$measuredEvidence.testedBinarySha256 -or
				[string]$build.corpus_lock_sha256 -cne [string]$measuredEvidence.corpusLockSha256 -or
				[string]$build.recipe_set_version -cne $catalogRevision -or
				[int]$coverage.case_count -lt 500 -or [int]$coverage.failed_case_count -ne 0 -or
				@(Compare-Object -ReferenceObject $attestedModels -DifferenceObject $reportedModels).Count -ne 0 -or
				[int]$record.caseCount -ne [int]$coverage.case_count) {
				throw "Measured quality evidence for '$runnerClass' failed semantic identity/coverage checks."
			}
		} else {
			$cases = @(Assert-ObjectProperty -Object $evidence -Name "cases" -Context "Original evidence '$runnerClass'")
			$candidateExecutableSha256 = [string](Assert-ObjectProperty -Object $evidence -Name "candidate_executable_sha256" -Context "Original evidence '$runnerClass'")
			$legacyExecutableSha256 = [string](Assert-ObjectProperty -Object $evidence -Name "legacy_executable_sha256" -Context "Original evidence '$runnerClass'")
			if ([string]$evidence.candidate_build_sha -cne $sourceSha -or [string]$evidence.profile -cne "Original" -or
				$candidateExecutableSha256 -cne [string]$measuredEvidence.testedBinarySha256 -or
				$legacyExecutableSha256 -cne [string]$measuredEvidence.legacyBinarySha256 -or
				$candidateExecutableSha256 -notmatch '^[0-9a-f]{64}$' -or $legacyExecutableSha256 -notmatch '^[0-9a-f]{64}$' -or
				$evidence.receiver_cleanup_enabled -ne $false -or
				[string]$evidence.transport_path -cne "client1-opus-server-client2" -or
				$cases.Count -ne 45 -or [int]$record.caseCount -ne 45) {
				throw "Original transport evidence for '$runnerClass' failed semantic identity/coverage checks."
			}
			foreach ($case in $cases) {
				if ([string]$case.enhancement_profile -cne "Original" -or [int64]$case.model_initialization_attempts -ne 0 -or
					[string]$case.candidate_executable_sha256 -cne $candidateExecutableSha256 -or
					[string]$case.legacy_executable_sha256 -cne $legacyExecutableSha256 -or
					[int64]$case.algorithmic_latency_samples -ne 0 -or [int64]$case.fallback_count -ne 0 -or
					[int64]$case.deadline_miss_count -ne 0 -or $case.original_receiver_fixed_timeline_passed -ne $true) {
					throw "Original transport evidence '$runnerClass' contains a failing case."
				}
			}
		}
	}
}
if (-not $runnerClasses.SetEquals([string[]]@("low-performance", "mainstream"))) {
	throw "Measured quality must contain exactly the protected low-performance and mainstream runner classes."
}

$signingRecord = Assert-ObjectProperty -Object $qualification -Name "signing" -Context "Qualification"
if ((Assert-ObjectProperty -Object $signingRecord -Name "required" -Context "Qualified signing") -ne $true -or
	(Assert-ObjectProperty -Object $signingRecord -Name "verified" -Context "Qualified signing") -ne $true) {
	throw "Qualification signing must report required=true and verified=true."
}
$null = Assert-QualifiedFile -Record $signingRecord -Path $SigningResultsPath -Label "Signing results" `
	-NameProperty "resultsFileName" -HashProperty "resultsSha256"
$signingResults = Read-ReleaseJson -Path $SigningResultsPath
$qualifiedSignerSubject = [string](Assert-ObjectProperty -Object $signingRecord -Name "expectedSignerSubject" -Context "Qualified signing")
if (-not [string]::IsNullOrWhiteSpace($ExpectedSignerSubject) -and $qualifiedSignerSubject -cne $ExpectedSignerSubject) {
	throw "Qualification signer subject does not match the promotion environment."
}
Assert-SigningResults -SigningResults $signingResults -ExpectedSignerSubject $qualifiedSignerSubject
$signedInstallerPath = "installer/$($installerFile.Name)"
$signedInstallerMatches = @($signingResults.files | Where-Object {
	([string]$_.path).Replace('\', '/') -ceq $signedInstallerPath
})
if ($signedInstallerMatches.Count -ne 1 -or
	[string](Assert-ObjectProperty -Object $signedInstallerMatches[0] -Name 'sha256' -Context 'Signed MSI result') -cne
		(Get-ReleaseFileSha256 -Path $installerFile.FullName)) {
	throw "Qualified installer does not match the exact Authenticode-verified MSI bytes."
}

Write-Host "Qualification verified for immutable build '$reportedBuildId' and artifact '$($artifactFile.Name)'."
