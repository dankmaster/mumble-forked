[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$BuildRoot,

	[string]$BuildType = "Release",
	[string]$CTestPath = "ctest",

	[Parameter(Mandatory = $true)]
	[string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
	throw "Build root does not exist: '$BuildRoot'."
}
$buildRootPath = (Resolve-Path -LiteralPath $BuildRoot).Path
$cachePath = Join-Path $buildRootPath "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
	throw "Release qualification requires CMakeCache.txt under '$buildRootPath'."
}
$cache = Get-Content -LiteralPath $cachePath
foreach ($option in @("tests", "benchmarks", "speech-cleanup-e2e")) {
	$matches = @($cache | Where-Object { $_ -match "^$([regex]::Escape($option)):BOOL=(?<value>.*)$" })
	if ($matches.Count -ne 1 -or $matches[0] -notmatch '=ON$') {
		throw "Release qualification requires CMake option $option=ON."
	}
}

$requiredTests = @(
	"DeepFilterNetCapiTests",
	"TestInputEnhancement",
	"TestInputEnhancementAuto",
	"TestInputEnhancementAutoV2",
	"TestInputEnhancementCalibration",
	"TestInputEnhancementCalibrationRuntime",
	"TestInputEnhancementPolicy",
	"TestInputEnhancementPolicyConfiguredKey",
	"TestInputEnhancementPolicyController",
	"TestInputEnhancementPackageVerifier",
	"TestInputEnhancementSettings",
	"TestAudioOutputMemorySample",
	"TestModernDialogControllers",
	"TestQmlQuickComponents",
	"TestUpdateHealth",
	"TestUpdaterHealthIntegration",
	"TestUpdaterProtocolV4Simulation",
	"TestSpeechCleanup",
	"SpeechCleanupBenchmarkSelfTest"
)
$listOutput = & $CTestPath --test-dir $buildRootPath -C $BuildType --show-only=json-v1 2>&1
if ($LASTEXITCODE -ne 0) {
	throw "ctest could not enumerate release qualification tests: $($listOutput -join [Environment]::NewLine)"
}
try {
	$testList = ($listOutput -join [Environment]::NewLine) | ConvertFrom-Json
} catch {
	throw "ctest --show-only=json-v1 did not produce valid JSON: $($_.Exception.Message)"
}
foreach ($requiredTest in $requiredTests) {
	if (@($testList.tests | Where-Object { [string]$_.name -ceq $requiredTest }).Count -ne 1) {
		throw "CTest must register exactly one '$requiredTest' test."
	}
}

$gateResults = New-Object System.Collections.Generic.List[object]
$allPassed = $true
foreach ($testName in $requiredTests) {
	$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
	& $CTestPath --test-dir $buildRootPath -C $BuildType --output-on-failure --no-tests=error -R "^$([regex]::Escape($testName))$"
	$exitCode = $LASTEXITCODE
	$stopwatch.Stop()
	$passed = $exitCode -eq 0
	if (-not $passed) {
		$allPassed = $false
	}
	$gateResults.Add([ordered]@{
		name       = $testName
		passed     = $passed
		exitCode   = $exitCode
		durationMs = [int64]$stopwatch.ElapsedMilliseconds
	})
}

$document = [ordered]@{
	schemaVersion = 1
	passed        = $allPassed
	buildType     = $BuildType
	cmakeOptions  = [ordered]@{
		tests            = $true
		benchmarks       = $true
		speechCleanupE2e = $true
	}
	gates         = $gateResults.ToArray()
}
Write-ReleaseJson -Value $document -Path $OutputPath

if (-not $allPassed) {
	throw "One or more mandatory input-enhancement release tests failed. Results were written to '$OutputPath'."
}
Assert-TestGateResults -GateResults $document
Write-Host "Mandatory input-enhancement runtime, settings, speech-cleanup, and benchmark gates passed."
