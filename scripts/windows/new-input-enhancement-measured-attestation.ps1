[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$SourceRoot,

	[Parameter(Mandatory = $true)]
	[string]$SourceSha,

	[Parameter(Mandatory = $true)]
	[string]$TestedBinaryPath,

	[Parameter(Mandatory = $true)]
	[string]$LowPerformanceEvidenceRoot,

	[Parameter(Mandatory = $true)]
	[string]$MainstreamEvidenceRoot,

	[Parameter(Mandatory = $true)]
	[string]$UnsignedModelManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$UnsignedRecipeManifestPath,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[1-9][0-9]*$')]
	[string]$QualityWorkflowRunId,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedHarnessSha256,

	[string]$PythonPath = "python",

	[Parameter(Mandatory = $true)]
	[string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

function Assert-ExactProperties {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Object,
		[Parameter(Mandatory = $true)]
		[string[]]$Names,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)

	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	$expected = @($Names | Sort-Object)
	if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
		throw "$Context has missing or unexpected properties. Expected '$($expected -join ', ')'; got '$($actual -join ', ')'."
	}
}

$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path
$expectedSha = Assert-FullGitSha -Sha $SourceSha
$testedBinary = Get-Item -LiteralPath $TestedBinaryPath -ErrorAction Stop
if ($testedBinary.PSIsContainer) {
	throw "Measured-quality tested binary must be a file."
}
$testedBinarySha256 = Get-ReleaseFileSha256 -Path $testedBinary.FullName

$runnerEvidence = New-Object System.Collections.Generic.List[object]
$sharedCorpusSha256 = ""
$sharedMixturePlanSha256 = ""
$sharedRecipeSetVersion = ""
$sharedModelHashes = @()
$sharedLegacyBinarySha256 = ""
foreach ($runner in @(
		@{ Class = "low-performance"; Root = $LowPerformanceEvidenceRoot },
		@{ Class = "mainstream"; Root = $MainstreamEvidenceRoot }
	)) {
	$root = (Resolve-Path -LiteralPath $runner.Root).Path
	$qualityPath = Join-Path $root "qualification.json"
	$originalPath = Join-Path $root "original-voice-qualification.json"
	$harnessProvenancePath = Join-Path $root "quality-harness-provenance.json"
	if (-not (Test-Path -LiteralPath $qualityPath -PathType Leaf) -or
		-not (Test-Path -LiteralPath $originalPath -PathType Leaf) -or
		-not (Test-Path -LiteralPath $harnessProvenancePath -PathType Leaf)) {
		throw "Measured evidence for '$($runner.Class)' is incomplete."
	}

	$harnessProvenance = Read-ReleaseJson -Path $harnessProvenancePath
	Assert-ExactProperties -Object $harnessProvenance -Names @(
		"harnessFileName", "harnessSha256", "kind", "qualityWorkflowRunId", "runnerClass", "schemaVersion", "sourceSha"
	) -Context "$($runner.Class) harness provenance"
	if ([int]$harnessProvenance.schemaVersion -ne 1 -or
		[string]$harnessProvenance.kind -cne "input-enhancement-quality-harness-provenance" -or
		[string]$harnessProvenance.sourceSha -cne $expectedSha -or
		[string]$harnessProvenance.qualityWorkflowRunId -cne $QualityWorkflowRunId -or
		[string]$harnessProvenance.runnerClass -cne $runner.Class -or
		[string]$harnessProvenance.harnessSha256 -cne $ExpectedHarnessSha256) {
		throw "Measured evidence for '$($runner.Class)' was not produced by the exact configured protected harness."
	}
	$harnessFileName = [string]$harnessProvenance.harnessFileName
	if ([string]::IsNullOrWhiteSpace($harnessFileName) -or
		$harnessFileName -cne [System.IO.Path]::GetFileName($harnessFileName)) {
		throw "Measured evidence for '$($runner.Class)' has an invalid protected harness file name."
	}

	& $PythonPath (Join-Path $sourceRootPath "scripts/audio-quality/run-ci-quality-gate.py") `
		--validate-only `
		--suite master_quality `
		--source-root $sourceRootPath `
		--output-root $root `
		--tested-binary $testedBinary.FullName `
		--staged-client-root $testedBinary.DirectoryName `
		--model-manifest $UnsignedModelManifestPath `
		--recipe-manifest $UnsignedRecipeManifestPath
	if ($LASTEXITCODE -ne 0) {
		throw "Measured evidence for '$($runner.Class)' failed semantic validation."
	}

	$quality = Read-ReleaseJson -Path $qualityPath
	$original = Read-ReleaseJson -Path $originalPath
	$build = Assert-ObjectProperty -Object $quality -Name "build" -Context "$($runner.Class) quality build"
	$coverage = Assert-ObjectProperty -Object $quality -Name "coverage" -Context "$($runner.Class) quality coverage"
	$qualitySha = Assert-FullGitSha -Sha ([string](Assert-ObjectProperty -Object $build -Name "git_sha" -Context "$($runner.Class) quality build"))
	if ($qualitySha -cne $expectedSha -or
		[string](Assert-ObjectProperty -Object $quality -Name "suite" -Context "$($runner.Class) quality") -cne "master_quality" -or
		[string](Assert-ObjectProperty -Object $quality -Name "status" -Context "$($runner.Class) quality") -cne "passed" -or
		[int](Assert-ObjectProperty -Object $coverage -Name "case_count" -Context "$($runner.Class) quality coverage") -lt 500) {
		throw "Measured evidence for '$($runner.Class)' does not attest a passing 500-case master_quality run for '$expectedSha'."
	}
	$evidenceBinarySha256 = [string](Assert-ObjectProperty -Object $build -Name "tested_binary_sha256" -Context "$($runner.Class) quality build")
	if ($evidenceBinarySha256 -cne $testedBinarySha256) {
		throw "Measured evidence for '$($runner.Class)' tested binary '$evidenceBinarySha256', not the exact unsigned staged binary '$testedBinarySha256'."
	}
	if ([string](Assert-ObjectProperty -Object $original -Name "candidate_build_sha" -Context "$($runner.Class) Original evidence") -cne $expectedSha) {
		throw "Original evidence for '$($runner.Class)' does not attest '$expectedSha'."
	}
	$originalCandidateExecutableSha256 = [string](Assert-ObjectProperty -Object $original `
		-Name "candidate_executable_sha256" -Context "$($runner.Class) Original evidence")
	if ($originalCandidateExecutableSha256 -cne $testedBinarySha256) {
		throw "Original evidence for '$($runner.Class)' tested candidate executable '$originalCandidateExecutableSha256', not '$testedBinarySha256'."
	}
	$legacyBinarySha256 = [string](Assert-ObjectProperty -Object $original `
		-Name "legacy_executable_sha256" -Context "$($runner.Class) Original evidence")
	if ($legacyBinarySha256 -cnotmatch '^[0-9a-f]{64}$') {
		throw "Original evidence for '$($runner.Class)' has an invalid legacy executable SHA256."
	}

	$corpusSha256 = [string](Assert-ObjectProperty -Object $build -Name "corpus_lock_sha256" -Context "$($runner.Class) quality build")
	$mixturePlanSha256 = [string](Assert-ObjectProperty -Object $build -Name "mixture_plan_sha256" -Context "$($runner.Class) quality build")
	$recipeSetVersion = [string](Assert-ObjectProperty -Object $build -Name "recipe_set_version" -Context "$($runner.Class) quality build")
	$modelHashes = @((Assert-ObjectProperty -Object $build -Name "model_hashes" -Context "$($runner.Class) quality build") | ForEach-Object { [string]$_ } | Sort-Object)
	if ([string]::IsNullOrWhiteSpace($sharedCorpusSha256)) {
		$sharedCorpusSha256 = $corpusSha256
		$sharedMixturePlanSha256 = $mixturePlanSha256
		$sharedRecipeSetVersion = $recipeSetVersion
		$sharedModelHashes = $modelHashes
		$sharedLegacyBinarySha256 = $legacyBinarySha256
	} elseif ($corpusSha256 -cne $sharedCorpusSha256 -or $mixturePlanSha256 -cne $sharedMixturePlanSha256 -or
		$recipeSetVersion -cne $sharedRecipeSetVersion -or
		$legacyBinarySha256 -cne $sharedLegacyBinarySha256 -or
		@(Compare-Object -ReferenceObject $sharedModelHashes -DifferenceObject $modelHashes).Count -ne 0) {
		throw "Protected runners did not test the same legacy binary, corpus, mixture plan, recipe set, and model hashes."
	}

	$runnerEvidence.Add([ordered]@{
		runnerClass             = $runner.Class
		harnessProvenanceSha256 = Get-ReleaseFileSha256 -Path $harnessProvenancePath
		qualityQualification    = [ordered]@{
			fileName = "quality-$($runner.Class).json"
			sha256   = Get-ReleaseFileSha256 -Path $qualityPath
			caseCount = [int]$coverage.case_count
		}
		originalVoiceQualification = [ordered]@{
			fileName = "original-voice-$($runner.Class).json"
			sha256   = Get-ReleaseFileSha256 -Path $originalPath
			caseCount = @($original.cases).Count
		}
	})
}

$document = [ordered]@{
	schemaVersion          = 1
	passed                 = $true
	suite                  = "master_quality"
	sourceSha              = $expectedSha
	testedBinaryFileName   = $testedBinary.Name
	testedBinarySha256     = $testedBinarySha256
	legacyBinarySha256     = $sharedLegacyBinarySha256
	harnessSha256          = $ExpectedHarnessSha256
	corpusLockSha256       = $sharedCorpusSha256
	mixturePlanSha256      = $sharedMixturePlanSha256
	recipeSetVersion       = $sharedRecipeSetVersion
	modelHashes            = $sharedModelHashes
	unsignedModelManifest = [ordered]@{
		fileName = (Get-Item -LiteralPath $UnsignedModelManifestPath).Name
		sha256   = Get-ReleaseFileSha256 -Path $UnsignedModelManifestPath
	}
	unsignedRecipeManifest = [ordered]@{
		fileName = (Get-Item -LiteralPath $UnsignedRecipeManifestPath).Name
		sha256   = Get-ReleaseFileSha256 -Path $UnsignedRecipeManifestPath
	}
	runners                = $runnerEvidence.ToArray()
	qualityWorkflowRunId   = $QualityWorkflowRunId
	createdAtUtc           = (Get-Date).ToUniversalTime().ToString("o")
}
Write-ReleaseJson -Value $document -Path $OutputPath
Write-Host "Bound measured master-quality evidence from both protected runner classes to candidate '$testedBinarySha256' and legacy '$sharedLegacyBinarySha256'."
