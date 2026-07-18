[CmdletBinding()]
param([string]$PythonPath = 'python')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$packager = Join-Path $repoRoot 'scripts\audio-quality\private_community_package.py'
$wrapper = Join-Path $repoRoot 'scripts\windows\new-input-enhancement-private-community-package.ps1'
$workflow = Join-Path $repoRoot '.github\workflows\input-enhancement-private-community-package.yml'
$documentation = Join-Path $repoRoot 'docs\input-enhancement-private-community-package.md'
foreach ($path in @($packager, $wrapper, $workflow, $documentation)) {
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Private-community packaging asset is missing: '$path'."
	}
}

$selfTestOutput = @(& $PythonPath $packager --self-test 2>&1)
if ($LASTEXITCODE -ne 0 -or ($selfTestOutput -join "`n") -notmatch 'self-test: ok') {
	throw "Private-community package fail-closed self-test failed:`n$($selfTestOutput -join [Environment]::NewLine)"
}

$tokens = $null
$errors = $null
[Management.Automation.Language.Parser]::ParseFile($wrapper, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -ne 0) {
	throw "Private-community PowerShell wrapper has parse errors: $($errors.Message -join '; ')"
}

$wrapperSource = Get-Content -LiteralPath $wrapper -Raw
foreach ($required in @(
	'CandidateBuildReceiptSha256', 'CoreMeasuredAttestationSha256',
	'assert-input-enhancement-package-manifests.ps1', 'assert-input-enhancement-package-signatures.ps1',
	'Assert-CanonicalInputEnhancementPolicy', 'RequireCurrentlyValid',
	'private_community_package.py', '--candidate-receipt-sha256', '--core-attestation-sha256',
	'UNSIGNED-PRIVATE-COMMUNITY', 'must not emit an MSI', 'refuses Azure/OIDC/private-key material'
)) {
	if (-not $wrapperSource.Contains($required)) {
		throw "Private-community wrapper lost required fail-closed contract '$required'."
	}
}
foreach ($forbidden in @('gh release', 'New-GitHubRelease', 'artifact-signing-action', 'azure/login')) {
	if ($wrapperSource -match [regex]::Escape($forbidden)) {
		throw "Private-community wrapper contains forbidden publication/signing capability '$forbidden'."
	}
}

$workflowSource = Get-Content -LiteralPath $workflow -Raw
foreach ($required in @(
	'workflow_dispatch:', 'actions: read', 'contents: read',
	'input-enhancement-community-package', 'new-input-enhancement-private-community-package.ps1',
	'actions/upload-artifact@', 'actions/download-artifact@', 'remote-reverify:',
	'UNSIGNED-PRIVATE-COMMUNITY'
)) {
	if (-not $workflowSource.Contains($required)) {
		throw "Private-community workflow lost required contract '$required'."
	}
}
foreach ($forbiddenPattern in @(
	'(?im)^\s*id-token\s*:\s*write\s*$',
	'(?im)^\s*contents\s*:\s*write\s*$',
	'(?im)^\s*environment\s*:',
	'\$\{\{\s*secrets\.',
	'(?i)azure/login',
	'(?i)artifact-signing-action',
	'(?i)\bgh\s+release\b',
	'(?i)api\.github\.com/.*/releases'
)) {
	if ($workflowSource -match $forbiddenPattern) {
		throw "Private-community workflow contains forbidden authority '$forbiddenPattern'."
	}
}

$blockedOutputParent = Join-Path ([IO.Path]::GetTempPath()) ('community-env-block-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $blockedOutputParent | Out-Null
$blockedOutputRoot = Join-Path $blockedOutputParent 'must-not-exist'
$env:AZURE_PRIVATE_COMMUNITY_SELFTEST = 'forbidden'
try {
	$blocked = $false
	try {
		& $wrapper `
			-SourceRoot $repoRoot -SourceSha ('0' * 40) -BuildNumber 1 `
			-BuildRoot $repoRoot -StageRoot $repoRoot `
			-CandidateBuildReceiptPath $wrapper -CandidateBuildReceiptSha256 ('0' * 64) `
			-CoreMeasuredAttestationPath $wrapper -CoreMeasuredAttestationSha256 ('0' * 64) `
			-Ed25519PublicKeyHex ('0' * 64) -AllowedOutputParent $blockedOutputParent `
			-OutputRoot $blockedOutputRoot
	} catch {
		$blocked = $_.Exception.Message -match 'refuses Azure/OIDC/private-key material'
	}
	if (-not $blocked -or (Test-Path -LiteralPath $blockedOutputRoot)) {
		throw 'Private-community wrapper did not reject Azure material before creating output.'
	}
} finally {
	Remove-Item Env:AZURE_PRIVATE_COMMUNITY_SELFTEST -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $blockedOutputParent -Recurse -Force -ErrorAction SilentlyContinue
}

$missingOutput = Join-Path ([IO.Path]::GetTempPath()) ('missing-community-package-' + [guid]::NewGuid().ToString('N'))
$validationOutput = @(& $PythonPath $packager --validate `
	--output-zip "$missingOutput.zip" --output-receipt "$missingOutput.receipt.json" `
	--output-sha256 "$missingOutput.sha256" 2>&1)
if ($LASTEXITCODE -eq 0) {
	throw 'Private-community validator accepted missing immutable outputs.'
}

$global:LASTEXITCODE = 0
Write-Host 'Unsigned private-community packaging fail-closed tests passed.'
