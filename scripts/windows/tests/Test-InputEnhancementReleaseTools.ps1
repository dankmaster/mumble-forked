[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptsRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Import-Module (Join-Path $scriptsRoot "InputEnhancementReleaseTools.psm1") -Force

function Invoke-NativeChecked {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Command,
		[Parameter(Mandatory = $true)]
		[string[]]$Arguments
	)

	& $Command @Arguments | Out-Null
	if ($LASTEXITCODE -ne 0) {
		throw "Native command '$Command $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
	}
}

function Assert-Throws {
	param(
		[Parameter(Mandatory = $true)]
		[scriptblock]$Script,
		[Parameter(Mandatory = $true)]
		[string]$Description
	)

	$threw = $false
	try {
		& $Script
	} catch {
		$threw = $true
		Write-Host "Expected rejection ($Description): $($_.Exception.Message)"
	}
	if (-not $threw) {
		throw "Expected failure did not occur: $Description."
	}
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "mumble-input-enhancement-release-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
try {
	$openSsl = Resolve-InputEnhancementOpenSsl
	$privateKeyPath = Join-Path $tempRoot 'release-ed25519-private.pem'
	Invoke-NativeChecked -Command $openSsl -Arguments @('genpkey', '-algorithm', 'Ed25519', '-out', $privateKeyPath)
	$privateKeyBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($privateKeyPath))
	$publicKeyHex = Get-Ed25519PublicKeyHexFromPrivateKey `
		-PrivateKeyBase64 $privateKeyBase64 -OpenSslPath $openSsl

	$sourceRoot = Join-Path $tempRoot "source"
	New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "init", "-q")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "config", "user.email", "release-tools@example.invalid")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "config", "user.name", "Release Tools Test")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "config", "commit.gpgsign", "false")
	[System.IO.File]::WriteAllText((Join-Path $sourceRoot "source.txt"), "clean`n")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "add", "source.txt")
	Invoke-NativeChecked -Command "git" -Arguments @("-C", $sourceRoot, "commit", "-q", "-m", "fixture")
	$sourceSha = (& git -C $sourceRoot rev-parse HEAD).Trim()

	$previewPolicyGate = Assert-InputEnhancementPromotionPolicy `
		-Channel preview -Available $true -ForceOriginal $false `
		-RecommendedProfile Balanced -RolloutEvidenceAvailable $false
	if ([bool]$previewPolicyGate.emergencyPolicy -or [bool]$previewPolicyGate.rolloutRequired -or
		[string]$previewPolicyGate.targetStage -cne 'none') {
		throw 'Preview promotion policy gate returned an invalid decision.'
	}
	Assert-Throws -Description 'preview Auto recommendation without stable evidence' -Script {
		Assert-InputEnhancementPromotionPolicy `
			-Channel preview -Available $true -ForceOriginal $false `
			-RecommendedProfile Auto -RolloutEvidenceAvailable $false | Out-Null
	}
	$emergencyPreviewGate = Assert-InputEnhancementPromotionPolicy `
		-Channel preview -Available $false -ForceOriginal $true `
		-RecommendedProfile Auto -RolloutEvidenceAvailable $false
	if (-not [bool]$emergencyPreviewGate.emergencyPolicy -or [bool]$emergencyPreviewGate.rolloutRequired) {
		throw 'Emergency preview policy gate returned an invalid decision.'
	}
	Assert-Throws -Description 'stable promotion without signed rollout evidence' -Script {
		Assert-InputEnhancementPromotionPolicy `
			-Channel stable -Available $true -ForceOriginal $false `
			-RecommendedProfile Balanced -RolloutEvidenceAvailable $false | Out-Null
	}
	$stableAutoGate = Assert-InputEnhancementPromotionPolicy `
		-Channel stable -Available $true -ForceOriginal $false `
		-RecommendedProfile Auto -RolloutEvidenceAvailable $true
	if (-not [bool]$stableAutoGate.rolloutRequired -or
		[string]$stableAutoGate.targetStage -cne 'auto-recommended') {
		throw 'Stable Auto promotion policy gate returned an invalid decision.'
	}
	$promotionWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-promote.yml'
	$promotionWorkflowSource = Get-Content -LiteralPath $promotionWorkflowPath -Raw
	if ([regex]::Matches($promotionWorkflowSource, 'Assert-InputEnhancementPromotionPolicy').Count -lt 3) {
		throw 'Promotion workflow must enforce the shared policy gate before download, evidence handling, and publication.'
	}
	$compatibilityMarker = '# Existing installed builds still request the legacy stable manifest.'
	$compatibilityOffset = $promotionWorkflowSource.IndexOf($compatibilityMarker, [StringComparison]::Ordinal)
	if ($compatibilityOffset -lt 0) {
		throw 'Promotion workflow is missing the stable compatibility publication section.'
	}
	$compatibilitySource = $promotionWorkflowSource.Substring($compatibilityOffset)
	$binaryUploadOffset = $compatibilitySource.IndexOf(
		'gh release upload $legacyTag @binaryUploads', [StringComparison]::Ordinal)
	$manifestUploadOffset = $compatibilitySource.IndexOf(
		'gh release upload $legacyTag .\mumble-forked-update.json', [StringComparison]::Ordinal)
	$modernCommitOffset = $compatibilitySource.IndexOf('& $publishModernChannel', [StringComparison]::Ordinal)
	if ($binaryUploadOffset -lt 0 -or $manifestUploadOffset -le $binaryUploadOffset -or
		$modernCommitOffset -le $manifestUploadOffset -or
		$compatibilitySource.Contains('delete-asset $legacyTag $installerName') -or
		$compatibilitySource.Contains('delete-asset $legacyTag ([string]$pointer.artifact.fileName)') -or
		-not $compatibilitySource.Contains('& $restoreCompatibility') -or
		$compatibilitySource.Substring($binaryUploadOffset, $manifestUploadOffset - $binaryUploadOffset).Contains('--clobber')) {
		throw 'Stable publication must retain prior binaries, publish compatibility first, and commit the modern signed pointer last with rollback.'
	}
	$qualityWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-quality.yml'
	$qualityWorkflowSource = Get-Content -LiteralPath $qualityWorkflowPath -Raw
	foreach ($requiredMarker in @(
		'vars.INPUT_ENHANCEMENT_QUALITY_HARNESS_SHA256',
		'$harnessSha256After',
		'quality-harness-provenance.json'
	)) {
		if (-not $qualityWorkflowSource.Contains($requiredMarker)) {
			throw "Measured quality workflow is missing protected harness binding marker '$requiredMarker'."
		}
	}
	$qualifiedWorkflowPath = Join-Path $scriptsRoot '..\..\.github\workflows\input-enhancement-qualified-build.yml'
	$qualifiedWorkflowSource = Get-Content -LiteralPath $qualifiedWorkflowPath -Raw
	if (-not $qualifiedWorkflowSource.Contains(
		'-ExpectedHarnessSha256 $env:INPUT_ENHANCEMENT_QUALITY_HARNESS_SHA256')) {
		throw 'Qualified build does not enforce the configured protected quality-harness SHA256.'
	}

	$stageRoot = Join-Path $tempRoot "stage"
	$modelDir = Join-Path $stageRoot "rnnoise"
	$crispModelDir = Join-Path $stageRoot "deepfilternet"
	New-Item -ItemType Directory -Force -Path $modelDir, $crispModelDir | Out-Null
	$modelAsset = Join-Path $modelDir "test-model.bin"
	$crispModelAsset = Join-Path $crispModelDir "test-model.bin"
	[System.IO.File]::WriteAllBytes($modelAsset, [byte[]](1, 2, 3, 4, 5))
	[System.IO.File]::WriteAllBytes($crispModelAsset, [byte[]](6, 7, 8, 9, 10))
	$modelDescriptorPath = Join-Path $tempRoot "model-descriptor.json"
	$recipeDescriptorPath = Join-Path $tempRoot "recipe-descriptor.json"
	Write-ReleaseJson -Path $modelDescriptorPath -Value ([ordered]@{
		schemaVersion = 1
		catalogRevision = "self-test-r1"
		models = @(
			[ordered]@{
				id = "rnnoise:embedded"
				version = "1"
				backend = "rnnoise"
				path = "rnnoise/test-model.bin"
				licenseSpdx = "BSD-3-Clause"
				sampleRateHz = 48000
				algorithmicLatencyMs = 30
				recipeCompatibility = @("input.balanced.rnnoise-embedded")
			},
			[ordered]@{
				id = "deepfilternet:balanced"
				version = "1"
				backend = "deepfilternet"
				path = "deepfilternet/test-model.bin"
				licenseSpdx = "MIT OR Apache-2.0"
				sampleRateHz = 48000
				algorithmicLatencyMs = 30
				recipeCompatibility = @("input.crisp.deepfilternet-balanced")
			}
		)
	})
	$recipes = @(
		[ordered]@{ id = "input.original"; revision = 1; profile = "Original"; modelIds = @() },
		[ordered]@{ id = "input.light.speex"; revision = 1; profile = "Light"; modelIds = @() },
		[ordered]@{ id = "input.balanced.rnnoise-embedded"; revision = 1; profile = "Balanced"; modelIds = @("rnnoise:embedded") },
		[ordered]@{ id = "input.crisp.deepfilternet-balanced"; revision = 1; profile = "Crisp"; modelIds = @("deepfilternet:balanced") },
		[ordered]@{ id = "input.auto.light.speex"; revision = 1; profile = "Auto"; modelIds = @() }
	)
	Write-ReleaseJson -Path $recipeDescriptorPath -Value ([ordered]@{
		schemaVersion = 1
		catalogRevision = "self-test-r1"
		recipes = $recipes
	})

	& (Join-Path $scriptsRoot "new-input-enhancement-package-manifests.ps1") `
		-StageRoot $stageRoot `
		-ModelDescriptorPath $modelDescriptorPath `
		-RecipeDescriptorPath $recipeDescriptorPath
	& (Join-Path $scriptsRoot "assert-input-enhancement-package-manifests.ps1") -StageRoot $stageRoot
	$modelManifestPath = Join-Path $stageRoot "input-models.json"
	$recipeManifestPath = Join-Path $stageRoot "input-recipes.json"
	$unsignedModelManifestPath = Join-Path $tempRoot "unsigned-input-models.json"
	$unsignedRecipeManifestPath = Join-Path $tempRoot "unsigned-input-recipes.json"
	Copy-Item -LiteralPath $modelManifestPath -Destination $unsignedModelManifestPath
	Copy-Item -LiteralPath $recipeManifestPath -Destination $unsignedRecipeManifestPath
	$modelManifestSignaturePath = "$modelManifestPath.sig"
	$recipeManifestSignaturePath = "$recipeManifestPath.sig"
	& (Join-Path $scriptsRoot 'protect-input-enhancement-json.ps1') `
		-InputPath $modelManifestPath -SignaturePath $modelManifestSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'protect-input-enhancement-json.ps1') `
		-InputPath $recipeManifestPath -SignaturePath $recipeManifestSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-package-signatures.ps1') `
		-StageRoot $stageRoot -PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	$tamperedSignaturePath = Join-Path $tempRoot 'tampered-model-signature.raw'
	$tamperedSignature = [IO.File]::ReadAllBytes($modelManifestSignaturePath)
	$tamperedSignature[0] = $tamperedSignature[0] -bxor 1
	[IO.File]::WriteAllBytes($tamperedSignaturePath, $tamperedSignature)
	Assert-Throws -Description 'detached signature byte tampering' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
			-InputPath $modelManifestPath -SignaturePath $tamperedSignaturePath `
			-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	}
	$modelManifest = Read-ReleaseJson -Path $modelManifestPath
	if ([string]$modelManifest.models[0].sha256 -cne (Get-ReleaseFileSha256 -Path $modelAsset)) {
		throw "Generated model manifest did not hash the actual staged asset."
	}
	[System.IO.File]::WriteAllBytes($modelAsset, [byte[]](5, 4, 3, 2, 1))
	Assert-Throws -Description "model asset hash mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-package-manifests.ps1") -StageRoot $stageRoot
	}
	[System.IO.File]::WriteAllBytes($modelAsset, [byte[]](1, 2, 3, 4, 5))

	$artifactPath = Join-Path $tempRoot "qualified-fixture.zip"
	$installerPath = Join-Path $tempRoot "mumble-forked-1.7.42.msi"
	[System.IO.File]::WriteAllBytes($installerPath, [byte[]](1, 3, 3, 7))
	$artifactFixtureRoot = Join-Path $tempRoot 'qualified-fixture-root'
	New-Item -ItemType Directory -Force -Path (Join-Path $artifactFixtureRoot 'installer') | Out-Null
	Copy-Item -LiteralPath $installerPath -Destination (Join-Path $artifactFixtureRoot 'installer')
	Compress-Archive -Path (Join-Path $artifactFixtureRoot '*') -DestinationPath $artifactPath
	$buildNumber = 42
	$buildId = Get-InputEnhancementBuildId -BuildNumber $buildNumber -SourceSha $sourceSha
	$rolloutRoot = Join-Path $tempRoot 'rollout'
	New-Item -ItemType Directory -Force -Path $rolloutRoot | Out-Null
	$rolloutPath = Join-Path $rolloutRoot 'input-enhancement-rollout.json'
	$rolloutSignaturePath = "$rolloutPath.sig"
	$rolloutEnd = [DateTimeOffset]::UtcNow.AddMinutes(-1)
	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-SourceChannel preview -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 10 -DistinctDevices 10 -TalkHours 50 `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		-EvidencePath $rolloutPath -SignaturePath $rolloutSignaturePath -PublicKeyHex $publicKeyHex `
		-TargetStage stable-opt-in -ExpectedBuildId $buildId -ExpectedRecipeSetVersion 'self-test-r1' `
		-OpenSslPath $openSsl

	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-SourceChannel preview -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-8) -WindowEndUtc $rolloutEnd -ObservationDays 7 `
		-DistinctUsers 9 -DistinctDevices 10 -TalkHours 50 `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	Assert-Throws -Description 'undersized signed stable rollout' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			-EvidencePath $rolloutPath -SignaturePath $rolloutSignaturePath -PublicKeyHex $publicKeyHex `
			-TargetStage stable-opt-in -ExpectedBuildId $buildId -ExpectedRecipeSetVersion 'self-test-r1' `
			-OpenSslPath $openSsl
	}

	& (Join-Path $scriptsRoot 'new-input-enhancement-rollout-qualification.ps1') `
		-SourceChannel stable -TestedBuildIds @($buildId) -RecipeSetVersion 'self-test-r1' `
		-WindowStartUtc $rolloutEnd.AddDays(-31) -WindowEndUtc $rolloutEnd -ObservationDays 30 `
		-DistinctUsers 50 -DistinctDevices 50 -TalkHours 500 -CrashFreeSessionRate 0.999 `
		-FallbackSessionRate 0.0009 -CallbackOverrunFrameRate 0.00009 `
		-ManualRollbackOrOptOutRate 0.099 -BlindAbResponses 25 -SelectedOverOriginalRate 0.60 `
		-DomainRnnoiseStatus completed -DomainRnnoiseOutcome embedded-retained `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
		-OutputPath $rolloutPath -SignaturePath $rolloutSignaturePath -OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		-EvidencePath $rolloutPath -SignaturePath $rolloutSignaturePath -PublicKeyHex $publicKeyHex `
		-TargetStage auto-recommended -ExpectedBuildId $buildId -ExpectedRecipeSetVersion 'self-test-r1' `
		-OpenSslPath $openSsl
	& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
		-EvidencePath $rolloutPath -SignaturePath $rolloutSignaturePath -PublicKeyHex $publicKeyHex `
		-TargetStage auto-default -ExpectedBuildId $buildId -ExpectedRecipeSetVersion 'self-test-r1' `
		-OpenSslPath $openSsl
	Add-Content -LiteralPath $rolloutPath -Value ' ' -NoNewline
	Assert-Throws -Description 'tampered rollout qualification' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-rollout-qualification.ps1') `
			-EvidencePath $rolloutPath -SignaturePath $rolloutSignaturePath -PublicKeyHex $publicKeyHex `
			-TargetStage auto-default -ExpectedBuildId $buildId -ExpectedRecipeSetVersion 'self-test-r1' `
			-OpenSslPath $openSsl
	}
	[System.IO.File]::WriteAllBytes((Join-Path $stageRoot 'mumble.exe'), [byte[]](11, 12, 13, 14, 15))
	[System.IO.File]::WriteAllBytes((Join-Path $stageRoot 'mumble-updater.exe'), [byte[]](21, 22, 23))
	[System.IO.File]::WriteAllBytes((Join-Path $stageRoot 'zlib1.dll'), [byte[]](31, 32, 33))
	$updatePackagePath = Join-Path $tempRoot "mumble-forked-1.7.42.mumble-update"
	& (Join-Path $scriptsRoot 'create-windows-update-package.ps1') `
		-StageRoot $stageRoot -OutputPath $updatePackagePath -Version '1.7.42' `
		-Build $buildNumber -Commit $sourceSha -RequireUpdaterRuntime -Validate
	$administrativeImageRoot = Join-Path $tempRoot 'administrative-image'
	$administrativePayloadRoot = Join-Path $administrativeImageRoot 'Program Files\Mumble\client'
	New-Item -ItemType Directory -Force -Path $administrativePayloadRoot | Out-Null
	Copy-Item -Path (Join-Path $stageRoot '*') -Destination $administrativePayloadRoot -Recurse -Force
	Copy-Item -LiteralPath $installerPath -Destination (Join-Path $administrativeImageRoot 'administrative-source.msi')
	[IO.File]::WriteAllBytes((Join-Path $administrativeImageRoot 'Mumble.cab'), [byte[]](41, 42, 43))
	$msiPayloadVerificationPath = Join-Path $tempRoot 'msi-payload-verification.json'
	& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
		-MsiPath $installerPath `
		-QualifiedPayloadRoot $stageRoot `
		-AdministrativeImageRoot $administrativeImageRoot `
		-OutputPath $msiPayloadVerificationPath
	$msiPayloadVerification = Read-ReleaseJson -Path $msiPayloadVerificationPath
	$expectedMsiPayloadFiles = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File).Count
	if ($msiPayloadVerification.passed -ne $true -or
		[int]$msiPayloadVerification.verifiedFileCount -ne $expectedMsiPayloadFiles -or
		[int]$msiPayloadVerification.allowedAdministrativeArtifactCount -ne 2) {
		throw 'MSI administrative-image fixture did not verify complete qualified payload parity.'
	}
	$administrativeModelPath = Join-Path $administrativePayloadRoot 'rnnoise\test-model.bin'
	$administrativeModelBytes = [IO.File]::ReadAllBytes($administrativeModelPath)
	[IO.File]::WriteAllBytes($administrativeModelPath, [byte[]](9, 9, 9))
	try {
		Assert-Throws -Description 'MSI administrative-image model mismatch' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		[IO.File]::WriteAllBytes($administrativeModelPath, $administrativeModelBytes)
	}
	$administrativeUpdaterPath = Join-Path $administrativePayloadRoot 'mumble-updater.exe'
	$administrativeUpdaterBytes = [IO.File]::ReadAllBytes($administrativeUpdaterPath)
	Remove-Item -LiteralPath $administrativeUpdaterPath -Force
	try {
		Assert-Throws -Description 'MSI administrative-image missing managed file' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		[IO.File]::WriteAllBytes($administrativeUpdaterPath, $administrativeUpdaterBytes)
	}
	$unexpectedManagedPath = Join-Path $administrativePayloadRoot 'unexpected-managed.dll'
	[IO.File]::WriteAllBytes($unexpectedManagedPath, [byte[]](51, 52, 53))
	try {
		Assert-Throws -Description 'MSI administrative-image unexpected managed file' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		Remove-Item -LiteralPath $unexpectedManagedPath -Force -ErrorAction SilentlyContinue
	}
	$undocumentedAdministrativePath = Join-Path $administrativeImageRoot 'undocumented-metadata.txt'
	[IO.File]::WriteAllText($undocumentedAdministrativePath, 'not allowed')
	try {
		Assert-Throws -Description 'MSI administrative-image undocumented outer artifact' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload.ps1') `
				-MsiPath $installerPath `
				-QualifiedPayloadRoot $stageRoot `
				-AdministrativeImageRoot $administrativeImageRoot
		}
	} finally {
		Remove-Item -LiteralPath $undocumentedAdministrativePath -Force -ErrorAction SilentlyContinue
	}
	# Qualification accepts only the report shape emitted by a real msiexec /a
	# run. The payload and administrative-artifact comparison above used a
	# deterministic pre-extracted fixture, so promote that fixture report to the
	# production method only after all negative parity tests have run.
	$qualificationMsiPayloadVerification = Read-ReleaseJson -Path $msiPayloadVerificationPath
	$qualificationMsiPayloadVerification.method = 'msiexec-administrative-image'
	Write-ReleaseJson -Path $msiPayloadVerificationPath -Value $qualificationMsiPayloadVerification
	& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload-verification.ps1') `
		-VerificationPath $msiPayloadVerificationPath `
		-MsiPath $installerPath `
		-UpdatePackagePath $updatePackagePath
	$incompleteMsiVerification = Read-ReleaseJson -Path $msiPayloadVerificationPath
	$incompleteMsiVerification.files = @($incompleteMsiVerification.files | Select-Object -Skip 1)
	$incompleteMsiVerification.verifiedFileCount = @($incompleteMsiVerification.files).Count
	$incompleteMsiVerificationPath = Join-Path $tempRoot 'incomplete-msi-payload-verification.json'
	Write-ReleaseJson -Path $incompleteMsiVerificationPath -Value $incompleteMsiVerification
	Assert-Throws -Description 'MSI verification attestation omits a managed update file' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-msi-payload-verification.ps1') `
			-VerificationPath $incompleteMsiVerificationPath `
			-MsiPath $installerPath `
			-UpdatePackagePath $updatePackagePath
	}
	New-Item -ItemType Directory -Force -Path (Join-Path $artifactFixtureRoot 'metadata') | Out-Null
	Copy-Item -LiteralPath $msiPayloadVerificationPath `
		-Destination (Join-Path $artifactFixtureRoot 'metadata\msi-payload-verification.json') -Force
	Remove-Item -LiteralPath $artifactPath -Force
	Compress-Archive -Path (Join-Path $artifactFixtureRoot '*') -DestinationPath $artifactPath
	$testedBinarySha256 = Get-ReleaseFileSha256 -Path (Join-Path $stageRoot 'mumble.exe')
	$corpusLockSha256 = ("cd" * 32)
	$mixturePlanSha256 = ("ef" * 32)
	$productModelHashes = @(
		(Get-ReleaseFileSha256 -Path $modelAsset),
		(Get-ReleaseFileSha256 -Path $crispModelAsset)
	) | Sort-Object
	$legacyExecutableSha256 = ("12" * 32)
	$qualityHarnessSha256 = ("34" * 32)
	$originalCases = @(1..45 | ForEach-Object {
		[ordered]@{
			enhancement_profile = 'Original'
			model_initialization_attempts = 0
			algorithmic_latency_samples = 0
			fallback_count = 0
			deadline_miss_count = 0
			candidate_executable_sha256 = $testedBinarySha256
			legacy_executable_sha256 = $legacyExecutableSha256
			original_receiver_fixed_timeline_passed = $true
		}
	})
	$qualityEvidence = {
		param([string]$RunnerClass)
		return [ordered]@{
			schema_version = 1
			suite = 'master_quality'
			status = 'passed'
			runnerClass = $RunnerClass
			build = [ordered]@{
				git_sha = $sourceSha
				tested_binary_sha256 = $testedBinarySha256
				corpus_lock_sha256 = $corpusLockSha256
				mixture_plan_sha256 = $mixturePlanSha256
				recipe_set_version = 'self-test-r1'
				model_hashes = $productModelHashes
			}
			coverage = [ordered]@{ case_count = 500; failed_case_count = 0 }
		}
	}
	$originalEvidence = {
		param([string]$RunnerClass)
		return [ordered]@{
			schema_version = 1
			candidate_build_sha = $sourceSha
			legacy_build_sha = $sourceSha
			candidate_executable_sha256 = $testedBinarySha256
			legacy_executable_sha256 = $legacyExecutableSha256
			profile = 'Original'
			receiver_cleanup_enabled = $false
			server_host = '127.0.0.1'
			transport_path = 'client1-opus-server-client2'
			corpus_sha256 = $corpusLockSha256
			runnerClass = $RunnerClass
			cases = $originalCases
		}
	}
	$measuredEvidenceFiles = [ordered]@{
		"quality-low-performance.json" = & $qualityEvidence 'low-performance'
		"original-voice-low-performance.json" = & $originalEvidence 'low-performance'
		"quality-mainstream.json" = & $qualityEvidence 'mainstream'
		"original-voice-mainstream.json" = & $originalEvidence 'mainstream'
	}
	foreach ($measuredEvidenceFile in $measuredEvidenceFiles.GetEnumerator()) {
		Write-ReleaseJson -Path (Join-Path $tempRoot $measuredEvidenceFile.Key) -Value $measuredEvidenceFile.Value
	}
	$fakePythonPath = Join-Path $tempRoot 'fake-python.cmd'
	[System.IO.File]::WriteAllText($fakePythonPath, "@exit /b 0`r`n", [System.Text.Encoding]::ASCII)
	$measuredRunnerRoots = @{
		'low-performance' = Join-Path $tempRoot 'measured-low-performance'
		'mainstream' = Join-Path $tempRoot 'measured-mainstream'
	}
	foreach ($runnerClass in @('low-performance', 'mainstream')) {
		$runnerRoot = $measuredRunnerRoots[$runnerClass]
		New-Item -ItemType Directory -Force -Path $runnerRoot | Out-Null
		Copy-Item -LiteralPath (Join-Path $tempRoot "quality-$runnerClass.json") `
			-Destination (Join-Path $runnerRoot 'qualification.json')
		Copy-Item -LiteralPath (Join-Path $tempRoot "original-voice-$runnerClass.json") `
			-Destination (Join-Path $runnerRoot 'original-voice-qualification.json')
		Write-ReleaseJson -Path (Join-Path $runnerRoot 'quality-harness-provenance.json') -Value ([ordered]@{
			schemaVersion = 1
			kind = 'input-enhancement-quality-harness-provenance'
			sourceSha = $sourceSha
			qualityWorkflowRunId = '123456'
			runnerClass = $runnerClass
			harnessFileName = 'protected-quality-harness.ps1'
			harnessSha256 = $qualityHarnessSha256
		})
	}
	$measuredScriptAttestationPath = Join-Path $tempRoot 'measured-script-attestation.json'
	$measuredScriptArguments = @{
		SourceRoot = $sourceRoot
		SourceSha = $sourceSha
		TestedBinaryPath = (Join-Path $stageRoot 'mumble.exe')
		LowPerformanceEvidenceRoot = $measuredRunnerRoots['low-performance']
		MainstreamEvidenceRoot = $measuredRunnerRoots['mainstream']
		UnsignedModelManifestPath = $unsignedModelManifestPath
		UnsignedRecipeManifestPath = $unsignedRecipeManifestPath
		QualityWorkflowRunId = '123456'
		ExpectedHarnessSha256 = $qualityHarnessSha256
		PythonPath = $fakePythonPath
		OutputPath = $measuredScriptAttestationPath
	}
	& (Join-Path $scriptsRoot 'new-input-enhancement-measured-attestation.ps1') @measuredScriptArguments
	$measuredScriptAttestation = Read-ReleaseJson -Path $measuredScriptAttestationPath
	if ([string]$measuredScriptAttestation.harnessSha256 -cne $qualityHarnessSha256 -or
		@($measuredScriptAttestation.runners | Where-Object {
			[string]$_.harnessProvenanceSha256 -cnotmatch '^[0-9a-f]{64}$'
		}).Count -ne 0) {
		throw 'Measured-attestation tool did not bind both runners to the protected harness provenance.'
	}
	$wrongHarnessArguments = $measuredScriptArguments.Clone()
	$wrongHarnessArguments.ExpectedHarnessSha256 = ('37' * 32)
	Assert-Throws -Description 'measured attestation configured harness mismatch' -Script {
		& (Join-Path $scriptsRoot 'new-input-enhancement-measured-attestation.ps1') @wrongHarnessArguments
	}

	$measuredEvidencePath = Join-Path $tempRoot "measured-quality-attestation.json"
	$measuredRunners = @(
		[ordered]@{
			runnerClass = "low-performance"
			harnessProvenanceSha256 = ("35" * 32)
			qualityQualification = [ordered]@{
				fileName = "quality-low-performance.json"
				sha256 = Get-ReleaseFileSha256 -Path (Join-Path $tempRoot "quality-low-performance.json")
				caseCount = 500
			}
			originalVoiceQualification = [ordered]@{
				fileName = "original-voice-low-performance.json"
				sha256 = Get-ReleaseFileSha256 -Path (Join-Path $tempRoot "original-voice-low-performance.json")
				caseCount = 45
			}
		},
		[ordered]@{
			runnerClass = "mainstream"
			harnessProvenanceSha256 = ("36" * 32)
			qualityQualification = [ordered]@{
				fileName = "quality-mainstream.json"
				sha256 = Get-ReleaseFileSha256 -Path (Join-Path $tempRoot "quality-mainstream.json")
				caseCount = 500
			}
			originalVoiceQualification = [ordered]@{
				fileName = "original-voice-mainstream.json"
				sha256 = Get-ReleaseFileSha256 -Path (Join-Path $tempRoot "original-voice-mainstream.json")
				caseCount = 45
			}
		}
	)
	Write-ReleaseJson -Path $measuredEvidencePath -Value ([ordered]@{
		schemaVersion = 1
		passed = $true
		suite = "master_quality"
		sourceSha = $sourceSha
		testedBinaryFileName = "mumble.exe"
		testedBinarySha256 = $testedBinarySha256
		legacyBinarySha256 = $legacyExecutableSha256
		harnessSha256 = $qualityHarnessSha256
		corpusLockSha256 = $corpusLockSha256
		mixturePlanSha256 = $mixturePlanSha256
		recipeSetVersion = "self-test-r1"
		modelHashes = $productModelHashes
		unsignedModelManifest = [ordered]@{
			fileName = (Get-Item $unsignedModelManifestPath).Name
			sha256 = Get-ReleaseFileSha256 -Path $unsignedModelManifestPath
		}
		unsignedRecipeManifest = [ordered]@{
			fileName = (Get-Item $unsignedRecipeManifestPath).Name
			sha256 = Get-ReleaseFileSha256 -Path $unsignedRecipeManifestPath
		}
		runners = $measuredRunners
		qualityWorkflowRunId = "123456"
		createdAtUtc = "2026-07-14T12:00:00.0000000+00:00"
	})
	$gatePath = Join-Path $tempRoot "test-gates.json"
	Write-ReleaseJson -Path $gatePath -Value ([ordered]@{
		schemaVersion = 1
		passed = $true
		gates = @(
			[ordered]@{ name = "DeepFilterNetCapiTests"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancement"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementAuto"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementCalibration"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementCalibrationRuntime"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPolicy"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPolicyConfiguredKey"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPolicyController"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementPackageVerifier"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestInputEnhancementSettings"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestModernDialogControllers"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestUpdateHealth"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestUpdaterHealthIntegration"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "TestSpeechCleanup"; passed = $true; exitCode = 0; durationMs = 1 },
			[ordered]@{ name = "SpeechCleanupBenchmarkSelfTest"; passed = $true; exitCode = 0; durationMs = 1 }
		)
	})
	$signerSubject = "CN=Mumble Input Enhancement Release Test"
	$signingPath = Join-Path $tempRoot "signing-results.json"
	Write-ReleaseJson -Path $signingPath -Value ([ordered]@{
		schemaVersion = 1
		verified = $true
		expectedSignerSubject = $signerSubject
		files = @(
			[ordered]@{
				path = "app/mumble.exe"
				sha256 = Get-ReleaseFileSha256 -Path (Join-Path $stageRoot 'mumble.exe')
				status = "Valid"
				timestamped = $true
				signerSubject = $signerSubject
			},
			[ordered]@{
				path = "installer/$((Get-Item -LiteralPath $installerPath).Name)"
				sha256 = Get-ReleaseFileSha256 -Path $installerPath
				status = "Valid"
				timestamped = $true
				signerSubject = $signerSubject
			}
		)
	})
	$qualificationPath = Join-Path $tempRoot "qualification.json"
	$buildNumber = 42
	$buildId = Get-InputEnhancementBuildId -BuildNumber $buildNumber -SourceSha $sourceSha

	& (Join-Path $scriptsRoot "new-input-enhancement-qualification.ps1") `
		-SourceRoot $sourceRoot `
		-SourceSha $sourceSha `
		-BuildNumber $buildNumber `
		-ArtifactPath $artifactPath `
		-InstallerPath $installerPath `
		-MsiPayloadVerificationPath $msiPayloadVerificationPath `
		-UpdatePackagePath $updatePackagePath `
		-ModelManifestPath $modelManifestPath `
		-RecipeManifestPath $recipeManifestPath `
		-UnsignedModelManifestPath $unsignedModelManifestPath `
		-UnsignedRecipeManifestPath $unsignedRecipeManifestPath `
		-ModelManifestSignaturePath $modelManifestSignaturePath `
		-RecipeManifestSignaturePath $recipeManifestSignaturePath `
		-Ed25519PublicKeyHex $publicKeyHex `
		-TestGateResultsPath $gatePath `
		-SigningResultsPath $signingPath `
		-MeasuredEvidencePath $measuredEvidencePath `
		-OutputPath $qualificationPath

	$assertArguments = @{
		QualificationPath = $qualificationPath
		ArtifactPath = $artifactPath
		InstallerPath = $installerPath
		MsiPayloadVerificationPath = $msiPayloadVerificationPath
		UpdatePackagePath = $updatePackagePath
		ModelManifestPath = $modelManifestPath
		RecipeManifestPath = $recipeManifestPath
		UnsignedModelManifestPath = $unsignedModelManifestPath
		UnsignedRecipeManifestPath = $unsignedRecipeManifestPath
		ModelManifestSignaturePath = $modelManifestSignaturePath
		RecipeManifestSignaturePath = $recipeManifestSignaturePath
		ExpectedEd25519PublicKeyHex = $publicKeyHex
		OpenSslPath = $openSsl
		TestGateResultsPath = $gatePath
		SigningResultsPath = $signingPath
		MeasuredEvidencePath = $measuredEvidencePath
		ExpectedSourceSha = $sourceSha
		ExpectedBuildId = $buildId
		ExpectedSignerSubject = $signerSubject
	}
	& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments

	$originalMeasuredEvidenceBytes = [System.IO.File]::ReadAllBytes($measuredEvidencePath)
	$tamperedMeasuredEvidence = Read-ReleaseJson -Path $measuredEvidencePath
	$tamperedMeasuredEvidence.harnessSha256 = ("37" * 32)
	Write-ReleaseJson -Path $measuredEvidencePath -Value $tamperedMeasuredEvidence
	Assert-Throws -Description "tampered protected quality-harness provenance" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	[System.IO.File]::WriteAllBytes($measuredEvidencePath, $originalMeasuredEvidenceBytes)

	$originalQualificationBytes = [System.IO.File]::ReadAllBytes($qualificationPath)
	$tamperedQualification = Read-ReleaseJson -Path $qualificationPath
	$tamperedQualification.measuredQuality.harnessSha256 = ("38" * 32)
	Write-ReleaseJson -Path $qualificationPath -Value $tamperedQualification
	Assert-Throws -Description "tampered qualified quality-harness provenance" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	[System.IO.File]::WriteAllBytes($qualificationPath, $originalQualificationBytes)

	$transportEvidencePath = Join-Path $tempRoot "original-voice-low-performance.json"
	$transportEvidence = Read-ReleaseJson -Path $transportEvidencePath
	$transportEvidence.transport_path = "mumble-opus"
	Write-ReleaseJson -Path $transportEvidencePath -Value $transportEvidence
	Assert-Throws -Description "quality/release Original transport contract mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	$transportEvidence.transport_path = "client1-opus-server-client2"
	Write-ReleaseJson -Path $transportEvidencePath -Value $transportEvidence

	$expandedArtifactRoot = Join-Path $tempRoot "qualified-expanded"
	& (Join-Path $scriptsRoot 'assert-windows-update-package.ps1') `
		-PackagePath $updatePackagePath -ExpectedCommit $sourceSha -ExpectedBuild $buildNumber `
		-ExpectedVersion '1.7.42' -RequireUpdaterRuntime -ExpandedPayloadPath $expandedArtifactRoot
	$packagedClientPath = Join-Path $expandedArtifactRoot "mumble.exe"
	$artifactHash = Get-ReleaseFileSha256 -Path $updatePackagePath
	$packagedClientHash = Get-ReleaseFileSha256 -Path $packagedClientPath
	$qualificationHash = Get-ReleaseFileSha256 -Path $qualificationPath
	$fixedModelHashes = @{
		Balanced = Get-ReleaseFileSha256 -Path $modelAsset
		Crisp = Get-ReleaseFileSha256 -Path $crispModelAsset
	}
	$fixedModelIds = @{
		Balanced = "rnnoise:embedded"
		Crisp = "deepfilternet:balanced"
	}
	$fixedRecipeIds = @{
		Balanced = "input.balanced.rnnoise-embedded"
		Crisp = "input.crisp.deepfilternet-balanced"
	}
	$releaseSmokeHarnessHash = ("13" * 32)
	$releaseSmokeServerHash = ("16" * 32)
	$releaseSmokeFixtureHashes = @{
		"stationary-hvac" = ("17" * 32)
		"transient-keyboard" = ("18" * 32)
		"competing-speech" = ("19" * 32)
	}
	$releaseSmokeFixtureManifestPath = Join-Path $tempRoot 'release-smoke-fixtures.json'
	Write-ReleaseJson -Path $releaseSmokeFixtureManifestPath -Value ([ordered]@{
		schemaVersion = 1
		audioFree = $true
		fixtures = @($releaseSmokeFixtureHashes.Keys | Sort-Object | ForEach-Object {
			[ordered]@{ id = "$_-fixture"; sha256 = $releaseSmokeFixtureHashes[$_] }
		})
	})
	$releaseSmokeFixtureManifestHash = Get-ReleaseFileSha256 -Path $releaseSmokeFixtureManifestPath
	$caseSetControls = New-Object System.Collections.Generic.List[object]
	$caseSetCases = New-Object System.Collections.Generic.List[object]
	foreach ($scene in @("stationary-hvac", "transient-keyboard", "competing-speech")) {
		foreach ($startup in @("cold", "warm")) {
			$preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
			$caseSetControls.Add([ordered]@{
				id = "$scene-original-$startup"; scene = $scene; startup = $startup
				preRollMs = $preRollMs; fixtureId = "$scene-fixture"
			})
		}
		foreach ($profile in @("Balanced", "Crisp")) {
			foreach ($startup in @("cold", "warm")) {
				$preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
				$caseSetCases.Add([ordered]@{
					id = "$scene-$($profile.ToLowerInvariant())-$startup"; scene = $scene; profile = $profile
					startup = $startup; preRollMs = $preRollMs; fixtureId = "$scene-fixture"
				})
			}
		}
	}
	$releaseSmokeCaseSetPath = Join-Path $tempRoot 'release-smoke-case-set.json'
	Write-ReleaseJson -Path $releaseSmokeCaseSetPath -Value ([ordered]@{
		schemaVersion = 1
		suite = 'input-enhancement-release-smoke-v1'
		originalControls = $caseSetControls.ToArray()
		cases = $caseSetCases.ToArray()
	})
	$releaseSmokeCaseSetHash = Get-ReleaseFileSha256 -Path $releaseSmokeCaseSetPath
	$releaseSmokeOriginalControls = New-Object System.Collections.Generic.List[object]
	foreach ($scene in @("stationary-hvac", "transient-keyboard", "competing-speech")) {
		foreach ($startup in @("cold", "warm")) {
			$controlId = "$scene-original-$startup"
			$releaseSmokeOriginalControls.Add([ordered]@{
				id = $controlId
				scene = $scene
				startup = $startup
				preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
				passed = $true
				fixtureId = "$scene-fixture"
				fixtureSha256 = $releaseSmokeFixtureHashes[$scene]
				artifactSha256 = ("ab" * 32)
				fixtureManifestSha256 = $releaseSmokeFixtureManifestHash
				caseSetSha256 = $releaseSmokeCaseSetHash
				senderExecutableSha256 = $packagedClientHash
				receiverExecutableSha256 = $packagedClientHash
				serverExecutableSha256 = $releaseSmokeServerHash
				harnessSha256 = $releaseSmokeHarnessHash
				routeVerified = $true
				encodedOpusPackets = 10
				receivedPcmFrames = 20
				receiverCleanupEnabled = $false
				postDecodeCleanupEnabled = $false
				deadlineMissCount = 0
				fixedTimelinePassed = $true
				timelineAlignment = 'fixed-original-no-correlation'
				onsetLossSamples = 0
				endLossSamples = 0
				missingTailSamples = 0
				receivedClippedSamples = 0
				referenceClippedSamples = 0
			})
		}
	}
	$releaseSmokeCases = New-Object System.Collections.Generic.List[object]
	foreach ($scene in @("stationary-hvac", "transient-keyboard", "competing-speech")) {
		foreach ($profile in @("Balanced", "Crisp")) {
			foreach ($startup in @("cold", "warm")) {
				$releaseSmokeCases.Add([ordered]@{
					id = "$scene-$($profile.ToLowerInvariant())-$startup"
					scene = $scene
					profile = $profile
					startup = $startup
					preRollMs = if ($startup -ceq "cold") { 0 } else { 300 }
					passed = $true
					artifactSha256 = $artifactHash
					fixtureId = "$scene-fixture"
					fixtureSha256 = $releaseSmokeFixtureHashes[$scene]
					fixtureManifestSha256 = $releaseSmokeFixtureManifestHash
					caseSetSha256 = $releaseSmokeCaseSetHash
					harnessSha256 = $releaseSmokeHarnessHash
					serverExecutableSha256 = $releaseSmokeServerHash
					senderExecutableSha256 = $packagedClientHash
					receiverExecutableSha256 = $packagedClientHash
					routeVerified = $true
					encodedOpusPackets = 10
					receivedPcmFrames = 20
					receiverCleanupEnabled = $false
					postDecodeCleanupEnabled = $false
					expectedModelSha256 = $fixedModelHashes[$profile]
					activeModelSha256 = $fixedModelHashes[$profile]
					expectedModelId = $fixedModelIds[$profile]
					activeModelId = $fixedModelIds[$profile]
					expectedRecipeId = $fixedRecipeIds[$profile]
					activeRecipeId = $fixedRecipeIds[$profile]
					expectedRecipeRevision = 1
					activeRecipeRevision = 1
					fallbackCount = 0
					modelHashMismatchCount = 0
					invalidOutputCount = 0
					tailErrorCount = 0
					latencyErrorCount = 0
					tailDrainExpectedFrames = 3
					tailDrainActualFrames = 3
					enhancementLatencyMs = if ($profile -ceq "Balanced") { 30.0 } else { 40.0 }
					fixedTimelinePassed = $true
					onsetLossSamples = 0
					endLossSamples = 0
					originalControlId = "$scene-original-$startup"
					originalControlArtifactSha256 = (("ab" * 32) -join "")
					timelineAlignment = 'fixed-paired-original-onset'
					missingTailSamples = 0
					receivedClippedSamples = 0
					referenceClippedSamples = 0
					callbackP99Ms = if ($profile -ceq 'Balanced') { 4.0 } else { 7.0 }
					workerP99Ms = if ($profile -ceq 'Crisp') { 7.0 } else { 0.0 }
					workerRtf = if ($profile -ceq 'Crisp') { 0.30 } else { 0.0 }
					workerPendingFrames = 0
					deadlineMissCount = 0
				})
			}
		}
	}
	$releaseSmokePath = Join-Path $tempRoot "release-smoke.json"
	Write-ReleaseJson -Path $releaseSmokePath -Value ([ordered]@{
		schemaVersion = 1
		suite = "input-enhancement-release-smoke-v1"
		passed = $true
		audioFree = $true
		sourceSha = $sourceSha
		buildId = $buildId
		createdAtUtc = "2026-07-14T12:00:00.0000000+00:00"
		artifact = [ordered]@{
			fileName = (Get-Item -LiteralPath $updatePackagePath).Name
			sha256 = $artifactHash
			size = [int64](Get-Item -LiteralPath $updatePackagePath).Length
		}
		qualification = [ordered]@{
			fileName = (Get-Item -LiteralPath $qualificationPath).Name
			sha256 = $qualificationHash
		}
		packagedClient = [ordered]@{
			relativePath = "mumble.exe"
			sha256 = $packagedClientHash
		}
		provenance = [ordered]@{
			harnessSha256 = $releaseSmokeHarnessHash
			fixtureManifestSha256 = $releaseSmokeFixtureManifestHash
			caseSetSha256 = $releaseSmokeCaseSetHash
			serverExecutableSha256 = $releaseSmokeServerHash
		}
		topology = [ordered]@{
			senderClient = "packaged-client-1"
			serverHost = "127.0.0.1"
			transport = "client1-opus-server-client2"
			receiverClient = "packaged-client-2"
			voiceProtocolModified = $false
			receiverCleanupEnabled = $false
			postDecodeCleanupEnabled = $false
		}
		originalControls = $releaseSmokeOriginalControls.ToArray()
		cases = $releaseSmokeCases.ToArray()
	})
	$releaseSmokeArguments = @{
		ReleaseSmokePath = $releaseSmokePath
		QualificationPath = $qualificationPath
		ModelManifestPath = $modelManifestPath
		RecipeManifestPath = $recipeManifestPath
		ModelManifestSignaturePath = $modelManifestSignaturePath
		RecipeManifestSignaturePath = $recipeManifestSignaturePath
		ArtifactPath = $updatePackagePath
		ExpandedArtifactRoot = $expandedArtifactRoot
		ExpectedSourceSha = $sourceSha
		ExpectedBuildId = $buildId
		ExpectedHarnessSha256 = $releaseSmokeHarnessHash
		FixtureManifestPath = $releaseSmokeFixtureManifestPath
		CaseSetPath = $releaseSmokeCaseSetPath
		ExpectedFixtureManifestSha256 = $releaseSmokeFixtureManifestHash
		ExpectedCaseSetSha256 = $releaseSmokeCaseSetHash
		ExpectedServerExecutableSha256 = $releaseSmokeServerHash
	}
	& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @releaseSmokeArguments

	$packagedModelSignaturePath = Join-Path $expandedArtifactRoot 'input-models.json.sig'
	$packagedModelSignatureBytes = [IO.File]::ReadAllBytes($packagedModelSignaturePath)
	$tamperedPackagedSignatureBytes = [byte[]]$packagedModelSignatureBytes.Clone()
	$tamperedPackagedSignatureBytes[0] = $tamperedPackagedSignatureBytes[0] -bxor 1
	[IO.File]::WriteAllBytes($packagedModelSignaturePath, $tamperedPackagedSignatureBytes)
	try {
		Assert-Throws -Description 'update-internal detached signature differs from qualified external signature' -Script {
			& (Join-Path $scriptsRoot 'assert-input-enhancement-package-manifest-binding.ps1') `
				-ExpandedPayloadRoot $expandedArtifactRoot `
				-ModelManifestPath $modelManifestPath `
				-RecipeManifestPath $recipeManifestPath `
				-ModelManifestSignaturePath $modelManifestSignaturePath `
				-RecipeManifestSignaturePath $recipeManifestSignaturePath
		}
	} finally {
		[IO.File]::WriteAllBytes($packagedModelSignaturePath, $packagedModelSignatureBytes)
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].fallbackCount = 1
	$badSmokePath = Join-Path $tempRoot "release-smoke-fallback.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke fallback" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].activeModelSha256 = (("ef" * 32) -join "")
	$badSmokePath = Join-Path $tempRoot "release-smoke-model-hash.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke active model hash mismatch" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].tailDrainActualFrames = 2
	$badSmokePath = Join-Path $tempRoot "release-smoke-tail.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke tail mismatch" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].enhancementLatencyMs = 30.1
	$badSmokePath = Join-Path $tempRoot "release-smoke-latency.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke latency budget" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].onsetLossSamples = 481
	$badSmokePath = Join-Path $tempRoot "release-smoke-fixed-timeline-onset.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke fixed-timeline onset loss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[2].originalControlArtifactSha256 = (("cd" * 32) -join "")
	$badSmokePath = Join-Path $tempRoot "release-smoke-unpaired-original-baseline.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke mismatched paired Original baseline" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedHarnessSha256 = ("20" * 32)
	Assert-Throws -Description "release smoke protected harness provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedFixtureManifestSha256 = ("21" * 32)
	Assert-Throws -Description "release smoke fixture manifest provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedCaseSetSha256 = ("22" * 32)
	Assert-Throws -Description "release smoke fixed case-set provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badArguments = $releaseSmokeArguments.Clone()
	$badArguments.ExpectedServerExecutableSha256 = ("23" * 32)
	Assert-Throws -Description "release smoke OG server binary provenance mismatch" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].fixtureSha256 = ("24" * 32)
	$badSmokePath = Join-Path $tempRoot "release-smoke-original-control-fixture.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke enhanced fixture differs from paired Original control" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].serverExecutableSha256 = ("25" * 32)
	$badSmokePath = Join-Path $tempRoot "release-smoke-case-server-provenance.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke case OG server provenance mismatch" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].originalControlId = "stationary-hvac-original-warm"
	$badSmokePath = Join-Path $tempRoot "release-smoke-wrong-original-control.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke wrong paired Original control" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].deadlineMissCount = 1
	$badSmokePath = Join-Path $tempRoot "release-smoke-deadline-miss.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke enhanced deadline miss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.originalControls[0].deadlineMissCount = 1
	$badSmokePath = Join-Path $tempRoot "release-smoke-original-control-deadline-miss.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke Original-control deadline miss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.originalControls[0].onsetLossSamples = 481
	$badSmokePath = Join-Path $tempRoot "release-smoke-original-control-onset-loss.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke Original-control onset loss" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases[0].callbackP99Ms = 5.1
	$badSmokePath = Join-Path $tempRoot "release-smoke-callback-p99.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke callback p99 budget" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.cases = @($badSmoke.cases | Select-Object -Skip 1)
	$badSmokePath = Join-Path $tempRoot "release-smoke-missing-case.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke missing fixed case" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke.topology.receiverCleanupEnabled = $true
	$badSmokePath = Join-Path $tempRoot "release-smoke-receiver-cleanup.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke receiver cleanup enabled" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$badSmoke = Read-ReleaseJson -Path $releaseSmokePath
	$badSmoke | Add-Member -NotePropertyName "rawAudioPath" -NotePropertyValue "forbidden.wav"
	$badSmokePath = Join-Path $tempRoot "release-smoke-audio-bearing.json"
	Write-ReleaseJson -Path $badSmokePath -Value $badSmoke
	Assert-Throws -Description "release smoke audio-bearing evidence" -Script {
		$badArguments = $releaseSmokeArguments.Clone()
		$badArguments.ReleaseSmokePath = $badSmokePath
		& (Join-Path $scriptsRoot "assert-input-enhancement-release-smoke.ps1") @badArguments
	}

	$originalArtifactBytes = [System.IO.File]::ReadAllBytes($artifactPath)
	$tamperedArtifactBytes = [byte[]]$originalArtifactBytes.Clone()
	$tamperedArtifactBytes[$tamperedArtifactBytes.Length - 1] = $tamperedArtifactBytes[$tamperedArtifactBytes.Length - 1] -bxor 1
	[System.IO.File]::WriteAllBytes($artifactPath, $tamperedArtifactBytes)
	Assert-Throws -Description "tampered artifact hash" -Script {
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @assertArguments
	}
	[System.IO.File]::WriteAllBytes($artifactPath, $originalArtifactBytes)
	Assert-Throws -Description "mismatched immutable build ID" -Script {
		$wrongArguments = $assertArguments.Clone()
		$wrongArguments.ExpectedBuildId = "mumble-forked-build-43-$($sourceSha.Substring(0, 12))"
		& (Join-Path $scriptsRoot "assert-input-enhancement-qualification.ps1") @wrongArguments
	}

	$previousPointerPath = Join-Path $tempRoot "previous-pointer.json"
	$previousTag = "mumble-forked-build-41-000000000000"
	Write-ReleaseJson -Path $previousPointerPath -Value ([ordered]@{
		schemaVersion = 1
		channel = "preview"
		immutableTag = $previousTag
		knownGoodTags = @($previousTag)
	})
	$previousPointerSignaturePath = "$previousPointerPath.sig"
	Protect-FileWithEd25519 -InputPath $previousPointerPath -SignaturePath $previousPointerSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	$policyPath = Join-Path $tempRoot 'input-enhancement-policy.json'
	$policyExpiration = [DateTimeOffset]::UtcNow.AddHours(1)
	$policyExpiration = [DateTimeOffset]::new(
		$policyExpiration.Year, $policyExpiration.Month, $policyExpiration.Day,
		$policyExpiration.Hour, $policyExpiration.Minute, $policyExpiration.Second,
		[TimeSpan]::Zero)
	& (Join-Path $scriptsRoot 'new-signed-input-enhancement-policy.ps1') `
		-Available $true -ForceOriginal $false -RecommendedProfile Balanced `
		-RecipeSetVersion 'self-test-r1' -MinBuild $buildNumber `
		-ExpiresAtUtc $policyExpiration -PrivateKeyBase64 $privateKeyBase64 `
		-ExpectedPublicKeyHex $publicKeyHex -OutputPath $policyPath -OpenSslPath $openSsl
	$expiredPolicyTime = [DateTimeOffset]::UtcNow
	$expiredPolicyTime = [DateTimeOffset]::new(
		$expiredPolicyTime.Year, $expiredPolicyTime.Month, $expiredPolicyTime.Day,
		$expiredPolicyTime.Hour, $expiredPolicyTime.Minute, $expiredPolicyTime.Second,
		[TimeSpan]::Zero).AddSeconds(-1)
	Assert-Throws -Description 'expired channel policy' -Script {
		& (Join-Path $scriptsRoot 'new-signed-input-enhancement-policy.ps1') `
			-Available $true -ForceOriginal $false -RecommendedProfile Balanced `
			-RecipeSetVersion 'self-test-r1' -MinBuild $buildNumber `
			-ExpiresAtUtc $expiredPolicyTime `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-OutputPath (Join-Path $tempRoot 'expired-policy.json') -OpenSslPath $openSsl
	}
	$channelPointerPath = Join-Path $tempRoot "channel-pointer.json"
	& (Join-Path $scriptsRoot "new-input-enhancement-channel-pointer.ps1") `
		-Channel preview `
		-Repository "example/mumble" `
		-QualificationPath $qualificationPath `
		-ReleaseSmokePath $releaseSmokePath `
		-Announcement "Self-test preview" `
		-PreviousPointerPath $previousPointerPath `
		-PreviousPointerSignaturePath $previousPointerSignaturePath `
		-PrivateKeyBase64 $privateKeyBase64 `
		-ExpectedPublicKeyHex $publicKeyHex `
		-PolicyPath $policyPath `
		-PolicySignaturePath "$policyPath.sig" `
		-OpenSslPath $openSsl `
		-OutputPath $channelPointerPath
	& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
		-InputPath $channelPointerPath -SignaturePath "$channelPointerPath.sig" `
		-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	$channelPointer = Read-ReleaseJson -Path $channelPointerPath
	if ([string]$channelPointer.artifact.fileName -cne (Get-Item -LiteralPath $updatePackagePath).Name -or
		[string]$channelPointer.artifact.sha256 -cne (Get-ReleaseFileSha256 -Path $updatePackagePath) -or
		[int64]$channelPointer.artifact.size -ne [int64](Get-Item -LiteralPath $updatePackagePath).Length) {
		throw "Signed channel pointer did not target the exact qualified .mumble-update package."
	}
	if ([string]$channelPointer.releaseSmoke.sha256 -cne (Get-ReleaseFileSha256 -Path $releaseSmokePath)) {
		throw "Signed channel pointer did not attest the exact release-smoke evidence."
	}
	if (@($channelPointer.knownGoodTags).Count -ne 2 -or
		[string]$channelPointer.knownGoodTags[0] -cne $buildId -or
		[string]$channelPointer.knownGoodTags[1] -cne $previousTag) {
		throw "Channel pointer did not retain the current and previous known-good immutable releases."
	}
	if ([string]$channelPointer.inputEnhancementPolicy.sha256 -cne (Get-ReleaseFileSha256 -Path $policyPath) -or
		[string]$channelPointer.inputEnhancementPolicy.signatureSha256 -cne (Get-ReleaseFileSha256 -Path "$policyPath.sig")) {
		throw "Signed channel pointer did not attest the exact signed policy files."
	}
	if ($channelPointer.PSObject.Properties['signedVerified']) {
		throw "Channel pointer still contains the obsolete non-cryptographic signedVerified claim."
	}
	$expectedPolicyBytes = '{"available":true,"expiresAt":"' +
		$policyExpiration.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture) +
		'","forceOriginal":false,"minBuild":42,"recipeSetVersion":"self-test-r1","recommendedProfile":"Balanced"}'
	if ([IO.File]::ReadAllText($policyPath, [Text.Encoding]::UTF8) -cne $expectedPolicyBytes) {
		throw "Signed policy generator did not emit the strict six-field canonical byte layout."
	}
	& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
		-InputPath $policyPath -SignaturePath "$policyPath.sig" `
		-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	[IO.File]::AppendAllText($policyPath, "`n", [Text.UTF8Encoding]::new($false))
	Assert-Throws -Description 'policy exact-byte tampering' -Script {
		& (Join-Path $scriptsRoot 'assert-input-enhancement-detached-signature.ps1') `
			-InputPath $policyPath -SignaturePath "$policyPath.sig" `
			-PublicKeyHex $publicKeyHex -OpenSslPath $openSsl
	}
	$unsignedQualificationPath = Join-Path $tempRoot "unsigned-qualification.json"
	$unsignedQualification = Read-ReleaseJson -Path $qualificationPath
	$unsignedQualification.artifact.signed = $false
	Write-ReleaseJson -Path $unsignedQualificationPath -Value $unsignedQualification
	Assert-Throws -Description "unsigned channel promotion" -Script {
		& (Join-Path $scriptsRoot "new-input-enhancement-channel-pointer.ps1") `
			-Channel stable -Repository "example/mumble" `
			-QualificationPath $unsignedQualificationPath `
			-ReleaseSmokePath $releaseSmokePath `
			-Announcement "Must fail" `
			-PrivateKeyBase64 $privateKeyBase64 -ExpectedPublicKeyHex $publicKeyHex `
			-PolicyPath $policyPath -PolicySignaturePath "$policyPath.sig" `
			-OpenSslPath $openSsl `
			-OutputPath (Join-Path $tempRoot "unsigned-pointer.json")
	}

	[System.IO.File]::WriteAllText((Join-Path $sourceRoot "source.txt"), "dirty`n")
	Assert-Throws -Description "dirty tracked source" -Script {
		& (Join-Path $scriptsRoot "new-input-enhancement-qualification.ps1") `
			-SourceRoot $sourceRoot -SourceSha $sourceSha -BuildNumber $buildNumber `
			-ArtifactPath $artifactPath -InstallerPath $installerPath -UpdatePackagePath $updatePackagePath `
			-MsiPayloadVerificationPath $msiPayloadVerificationPath `
			-ModelManifestPath $modelManifestPath `
			-RecipeManifestPath $recipeManifestPath `
			-UnsignedModelManifestPath $unsignedModelManifestPath `
			-UnsignedRecipeManifestPath $unsignedRecipeManifestPath `
			-ModelManifestSignaturePath $modelManifestSignaturePath `
			-RecipeManifestSignaturePath $recipeManifestSignaturePath `
			-Ed25519PublicKeyHex $publicKeyHex -OpenSslPath $openSsl `
			-TestGateResultsPath $gatePath `
			-SigningResultsPath $signingPath -MeasuredEvidencePath $measuredEvidencePath `
			-OutputPath (Join-Path $tempRoot "dirty-qualification.json")
	}

	$validSigningConfig = @{
		AzureClientId = "11111111-1111-1111-1111-111111111111"
		AzureTenantId = "22222222-2222-2222-2222-222222222222"
		AzureSubscriptionId = "33333333-3333-3333-3333-333333333333"
		Endpoint = "https://neu.codesigning.azure.net/"
		SigningAccountName = "mumble-signing"
		CertificateProfileName = "public-release"
		ExpectedSignerSubject = $signerSubject
		Ed25519PrivateKeyBase64 = $privateKeyBase64
		Ed25519PublicKeyHex = $publicKeyHex
		OpenSslPath = $openSsl
	}
	& (Join-Path $scriptsRoot "assert-input-enhancement-signing-config.ps1") @validSigningConfig
	Assert-Throws -Description "mismatched Ed25519 private/public release key" -Script {
		$badKeyConfig = $validSigningConfig.Clone()
		$badKeyConfig.Ed25519PublicKeyHex = ('00' * 32)
		& (Join-Path $scriptsRoot "assert-input-enhancement-signing-config.ps1") @badKeyConfig
	}
	Assert-Throws -Description "missing fail-closed signing subject" -Script {
		$missingConfig = $validSigningConfig.Clone()
		$missingConfig.ExpectedSignerSubject = ""
		& (Join-Path $scriptsRoot "assert-input-enhancement-signing-config.ps1") @missingConfig
	}

	$repositoryRoot = (Resolve-Path (Join-Path $scriptsRoot '..\..')).Path
	Assert-Throws -Description 'release CMake configuration without embedded Ed25519 public key' -Script {
		Invoke-NativeChecked -Command 'cmake' -Arguments @(
			'-S', $repositoryRoot,
			'-B', (Join-Path $tempRoot 'cmake-missing-policy-key'),
			'-Dinput-enhancement-signed-policy-required=ON'
		)
	}
	Assert-Throws -Description 'release CMake configuration with malformed Ed25519 public key' -Script {
		Invoke-NativeChecked -Command 'cmake' -Arguments @(
			'-S', $repositoryRoot,
			'-B', (Join-Path $tempRoot 'cmake-invalid-policy-key'),
			'-DMUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX=not-a-key'
		)
	}

	if ($env:OS -eq "Windows_NT") {
		$fakeBuildRoot = Join-Path $tempRoot "fake-build"
		New-Item -ItemType Directory -Force -Path $fakeBuildRoot | Out-Null
		[System.IO.File]::WriteAllLines(
			(Join-Path $fakeBuildRoot "CMakeCache.txt"),
			@("tests:BOOL=ON", "benchmarks:BOOL=ON"),
			[System.Text.UTF8Encoding]::new($false)
		)
		$fakeCTest = Join-Path $tempRoot "fake-ctest.cmd"
		[System.IO.File]::WriteAllLines(
			$fakeCTest,
			@(
				"@echo off",
				"echo %* | findstr /C:`"--show-only=json-v1`" >nul",
				"if %errorlevel%==0 (",
				"  echo {`"kind`":`"ctestInfo`",`"version`":{`"major`":1,`"minor`":0},`"tests`":[{`"name`":`"DeepFilterNetCapiTests`"},{`"name`":`"TestInputEnhancement`"},{`"name`":`"TestInputEnhancementAuto`"},{`"name`":`"TestInputEnhancementCalibration`"},{`"name`":`"TestInputEnhancementCalibrationRuntime`"},{`"name`":`"TestInputEnhancementPolicy`"},{`"name`":`"TestInputEnhancementPolicyConfiguredKey`"},{`"name`":`"TestInputEnhancementPolicyController`"},{`"name`":`"TestInputEnhancementPackageVerifier`"},{`"name`":`"TestInputEnhancementSettings`"},{`"name`":`"TestModernDialogControllers`"},{`"name`":`"TestUpdateHealth`"},{`"name`":`"TestUpdaterHealthIntegration`"},{`"name`":`"TestSpeechCleanup`"},{`"name`":`"SpeechCleanupBenchmarkSelfTest`"}]}",
				"  exit /b 0",
				")",
				"exit /b 0"
			),
			[System.Text.UTF8Encoding]::new($false)
		)
		$fakeGatePath = Join-Path $tempRoot "fake-test-gates.json"
		& (Join-Path $scriptsRoot "invoke-input-enhancement-release-tests.ps1") `
			-BuildRoot $fakeBuildRoot `
			-CTestPath $fakeCTest `
			-OutputPath $fakeGatePath
		Assert-TestGateResults -GateResults (Read-ReleaseJson -Path $fakeGatePath)
		[System.IO.File]::WriteAllLines(
			(Join-Path $fakeBuildRoot "CMakeCache.txt"),
			@("tests:BOOL=ON", "benchmarks:BOOL=OFF"),
			[System.Text.UTF8Encoding]::new($false)
		)
		Assert-Throws -Description "release build with benchmarks disabled" -Script {
			& (Join-Path $scriptsRoot "invoke-input-enhancement-release-tests.ps1") `
				-BuildRoot $fakeBuildRoot `
				-CTestPath $fakeCTest `
				-OutputPath (Join-Path $tempRoot "must-not-pass-gates.json")
		}
	}

	if ($env:OS -eq "Windows_NT") {
		$unsignedRoot = Join-Path $tempRoot "unsigned"
		New-Item -ItemType Directory -Force -Path $unsignedRoot | Out-Null
		[System.IO.File]::WriteAllText((Join-Path $unsignedRoot "mumble.exe"), "not signed")
		Assert-Throws -Description "unsigned stable candidate" -Script {
			& (Join-Path $scriptsRoot "assert-input-enhancement-signatures.ps1") `
				-Root $unsignedRoot `
				-ExpectedSignerSubject $signerSubject `
				-OutputPath (Join-Path $tempRoot "unsigned-results.json")
		}
	}

	Write-Host "All input-enhancement release tooling self-tests passed."
} finally {
	Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
