[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$SourceRoot,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$SourceSha,
	[Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int]$BuildNumber,
	[Parameter(Mandatory = $true)] [string]$BuildRoot,
	[Parameter(Mandatory = $true)] [string]$StageRoot,
	[Parameter(Mandatory = $true)] [string]$CandidateBuildReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$CandidateBuildReceiptSha256,
	[Parameter(Mandatory = $true)] [string]$CoreMeasuredAttestationPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$CoreMeasuredAttestationSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$Ed25519PublicKeyHex,
	[Parameter(Mandatory = $true)] [string]$AllowedOutputParent,
	[Parameter(Mandatory = $true)] [string]$OutputRoot,
	[string]$OpenSslPath = '',
	[string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'Private-community Windows packaging is Windows-only.' }
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

# This path consumes only already-signed public artifacts. It must never be
# turned into a convenient bridge to Azure, OIDC, or any production private
# key. The separate Azure workflow remains the sole public-release signer.
$forbiddenEnvironment = @(Get-ChildItem Env: | Where-Object {
	$_.Name -match '^(AZURE_|ACTIONS_ID_TOKEN_REQUEST_|INPUT_ENHANCEMENT_ED25519_PRIVATE_KEY|AZURE_ARTIFACT_SIGNING|TRUSTED_SIGNING)' -or
	$_.Name -match '(?i)(PRODUCTION.*PRIVATE|PROD.*SIGNING.*KEY)'
})
if ($forbiddenEnvironment.Count -ne 0) {
	throw "Unsigned private-community packaging refuses Azure/OIDC/private-key material: $($forbiddenEnvironment.Name -join ', ')."
}

$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\', '/')
$buildRootPath = (Resolve-Path -LiteralPath $BuildRoot).Path.TrimEnd('\', '/')
$stageRootPath = (Resolve-Path -LiteralPath $StageRoot).Path.TrimEnd('\', '/')
$candidateReceipt = Get-Item -LiteralPath $CandidateBuildReceiptPath -Force -ErrorAction Stop
$coreAttestation = Get-Item -LiteralPath $CoreMeasuredAttestationPath -Force -ErrorAction Stop
foreach ($item in @($candidateReceipt, $coreAttestation)) {
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw "Protected packaging input '$($item.FullName)' must be a regular non-reparse file."
	}
}
if ((Get-ReleaseFileSha256 -Path $candidateReceipt.FullName) -cne $CandidateBuildReceiptSha256) {
	throw 'Candidate build receipt differs from its protected SHA-256.'
}
if ((Get-ReleaseFileSha256 -Path $coreAttestation.FullName) -cne $CoreMeasuredAttestationSha256) {
	throw 'Core measured-quality attestation differs from its protected SHA-256.'
}

$publicKey = Assert-Ed25519PublicKeyHex -PublicKeyHex $Ed25519PublicKeyHex `
	-Context 'Private-community operator/test Ed25519 public key'
$openssl = Resolve-InputEnhancementOpenSsl -OpenSslPath $OpenSslPath
$modelManifestPath = Join-Path $stageRootPath 'input-models.json'
$recipeManifestPath = Join-Path $stageRootPath 'input-recipes.json'
$policyPath = Join-Path $stageRootPath 'input-enhancement-policy.json'
$policySignaturePath = "$policyPath.sig"

& (Join-Path $PSScriptRoot 'assert-input-enhancement-package-manifests.ps1') `
	-StageRoot $stageRootPath -ModelManifestPath $modelManifestPath -RecipeManifestPath $recipeManifestPath
& (Join-Path $PSScriptRoot 'assert-input-enhancement-package-signatures.ps1') `
	-StageRoot $stageRootPath -PublicKeyHex $publicKey -OpenSslPath $openssl
if (-not (Test-Ed25519DetachedSignature -InputPath $policyPath -SignaturePath $policySignaturePath `
	-PublicKeyHex $publicKey -OpenSslPath $openssl)) {
	throw 'Packaged bootstrap policy has no valid detached Ed25519 signature.'
}
$recipeManifest = Read-ReleaseJson -Path $recipeManifestPath
$recipeSetVersion = [string](Assert-ObjectProperty -Object $recipeManifest `
	-Name 'catalogRevision' -Context 'Private-community recipe manifest')
$policy = Assert-CanonicalInputEnhancementPolicy -Path $policyPath `
	-ExpectedMinBuild ([uint64]$BuildNumber) -ExpectedRecipeSetVersion $recipeSetVersion -RequireCurrentlyValid
if ($policy.available -ne $true -or $policy.forceOriginal -ne $false) {
	throw 'Private-community bootstrap policy must explicitly enable input enhancement.'
}
if ([string]$policy.recommendedProfile -ceq 'Auto') {
	throw 'Private-community bootstrap policy must not recommend experimental Auto.'
}

$outputRootPath = Initialize-InputEnhancementRehearsalOutputRoot `
	-OutputRoot $OutputRoot -AllowedOutputParent $AllowedOutputParent -SourceRoot $sourceRootPath
$createdOutput = $true
try {
	$outputPrefix = $outputRootPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	foreach ($protectedRoot in @($buildRootPath, $stageRootPath)) {
		$protectedPrefix = $protectedRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
		if ($outputRootPath.Equals($protectedRoot, [StringComparison]::OrdinalIgnoreCase) -or
			$outputRootPath.StartsWith($protectedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
			$protectedRoot.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
			throw 'Private-community output root must not overlap the build or staged payload.'
		}
	}

	$baseName = "mumble-1.7.$BuildNumber-$($SourceSha.Substring(0, 12))-UNSIGNED-PRIVATE-COMMUNITY"
	$zipPath = Join-Path $outputRootPath "$baseName.zip"
	$receiptPath = Join-Path $outputRootPath "$baseName.receipt.json"
	$sha256Path = Join-Path $outputRootPath "$baseName.sha256"
	$packager = Join-Path $PSScriptRoot '..\audio-quality\private_community_package.py'
	$arguments = @(
		$packager, '--create',
		'--stage-root', $stageRootPath,
		'--candidate-receipt', $candidateReceipt.FullName,
		'--candidate-receipt-sha256', $CandidateBuildReceiptSha256,
		'--core-attestation', $coreAttestation.FullName,
		'--core-attestation-sha256', $CoreMeasuredAttestationSha256,
		'--source-root', $sourceRootPath,
		'--source-sha', $SourceSha,
		'--build-root', $buildRootPath,
		'--build-number', $BuildNumber.ToString([Globalization.CultureInfo]::InvariantCulture),
		'--public-key-hex', $publicKey,
		'--output-zip', $zipPath,
		'--output-receipt', $receiptPath,
		'--output-sha256', $sha256Path
	)
	$output = @(& $PythonPath @arguments 2>&1)
	$exitCode = $LASTEXITCODE
	foreach ($line in $output) { Write-Host ([string]$line) }
	if ($exitCode -ne 0) {
		throw 'Fail-closed private-community package creation rejected the candidate.'
	}

	$verificationOutput = @(& $PythonPath $packager --validate `
		--output-zip $zipPath --output-receipt $receiptPath --output-sha256 $sha256Path 2>&1)
	$verificationExitCode = $LASTEXITCODE
	foreach ($line in $verificationOutput) { Write-Host ([string]$line) }
	if ($verificationExitCode -ne 0) {
		throw 'Private-community package failed immediate byte-for-byte reverification.'
	}

	$actualFiles = @(Get-ChildItem -LiteralPath $outputRootPath -Force -File)
	$expectedNames = @((Split-Path -Leaf $zipPath), (Split-Path -Leaf $receiptPath), (Split-Path -Leaf $sha256Path)) | Sort-Object
	$actualNames = @($actualFiles.Name | Sort-Object)
	if (@(Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames).Count -ne 0 -or
		@(Get-ChildItem -LiteralPath $outputRootPath -Force -Directory).Count -ne 0) {
		throw 'Private-community output contains an unexpected file or directory.'
	}
	if (@($actualFiles | Where-Object { $_.Extension -ieq '.msi' }).Count -ne 0) {
		throw 'Pre-Azure private-community packaging must not emit an MSI.'
	}

	$receipt = Read-ReleaseJson -Path $receiptPath
	Write-Host "Created immutable UNSIGNED PRIVATE COMMUNITY ZIP '$zipPath'."
	$receipt
} catch {
	if ($createdOutput -and (Test-Path -LiteralPath $outputRootPath)) {
		# The exact output root was created above as a new direct child of the
		# operator-supplied parent. It is safe to remove on a failed transaction.
		Remove-Item -LiteralPath $outputRootPath -Recurse -Force -ErrorAction SilentlyContinue
	}
	throw
}
