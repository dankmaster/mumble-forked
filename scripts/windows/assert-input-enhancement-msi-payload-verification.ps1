[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$VerificationPath,

	[Parameter(Mandatory = $true)]
	[string]$MsiPath,

	[Parameter(Mandatory = $true)]
	[string]$UpdatePackagePath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

function Assert-ExactProperties {
	param([object]$Object, [string[]]$Names, [string]$Context)
	$actual = @($Object.PSObject.Properties.Name)
	$missing = @($Names | Where-Object { $_ -cnotin $actual })
	$unexpected = @($actual | Where-Object { $_ -cnotin $Names })
	if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
		throw "$Context schema mismatch. Missing: [$($missing -join ', ')]; unexpected: [$($unexpected -join ', ')]."
	}
}

$verification = Read-ReleaseJson -Path $VerificationPath
Assert-ExactProperties -Object $verification -Context 'MSI payload verification' -Names @(
	'schemaVersion', 'passed', 'method', 'msi', 'administrativePayloadRoot',
	'verifiedFileCount', 'files', 'allowedAdministrativeArtifactCount',
	'allowedAdministrativeArtifacts'
)
if ([int]$verification.schemaVersion -ne 1 -or $verification.passed -ne $true -or
	[string]$verification.method -cne 'msiexec-administrative-image') {
	throw "Qualified MSI payload verification must be a passing real msiexec administrative extraction."
}
$null = Assert-SafeRelativeReleasePath -Path ([string]$verification.administrativePayloadRoot) `
	-Context 'Administrative payload root'

$msi = Get-Item -LiteralPath $MsiPath -ErrorAction Stop
$msiRecord = Assert-ObjectProperty -Object $verification -Name 'msi' -Context 'MSI payload verification'
Assert-ExactProperties -Object $msiRecord -Context 'MSI payload verification installer' `
	-Names @('fileName', 'sha256', 'size')
if ([string]$msiRecord.fileName -cne $msi.Name -or
	[string]$msiRecord.sha256 -cne (Get-ReleaseFileSha256 -Path $msi.FullName) -or
	[int64]$msiRecord.size -ne [int64]$msi.Length) {
	throw "MSI payload verification is not bound to the qualified installer bytes."
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
	("mumble-msi-verification-assert-" + [guid]::NewGuid().ToString('N'))
try {
	& (Join-Path $PSScriptRoot 'assert-windows-update-package.ps1') `
		-PackagePath $UpdatePackagePath -ExpandedPayloadPath $temporaryRoot
	$actualFiles = New-Object 'System.Collections.Generic.Dictionary[string,object]' `
		([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($file in @(Get-ChildItem -LiteralPath $temporaryRoot -Recurse -File)) {
		$relativePath = $file.FullName.Substring($temporaryRoot.Length).TrimStart('\', '/').Replace('\', '/')
		$relativePath = Assert-SafeRelativeReleasePath -Path $relativePath -Context 'Expanded update path'
		if ($actualFiles.ContainsKey($relativePath)) {
			throw "Expanded update contains duplicate case-insensitive path '$relativePath'."
		}
		$actualFiles.Add($relativePath, $file)
	}

	$reportedFiles = @(Assert-ObjectProperty -Object $verification -Name 'files' -Context 'MSI payload verification')
	if ([int]$verification.verifiedFileCount -ne $reportedFiles.Count -or
		$reportedFiles.Count -ne $actualFiles.Count -or $reportedFiles.Count -eq 0) {
		throw "MSI payload verification file count does not match the complete qualified update payload."
	}
	$seenPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($record in $reportedFiles) {
		Assert-ExactProperties -Object $record -Context 'MSI payload file record' -Names @('path', 'sha256', 'size')
		$relativePath = Assert-SafeRelativeReleasePath -Path ([string]$record.path) -Context 'MSI payload file path'
		if (-not $seenPaths.Add($relativePath) -or -not $actualFiles.ContainsKey($relativePath)) {
			throw "MSI payload verification contains unexpected or duplicate path '$relativePath'."
		}
		$actual = $actualFiles[$relativePath]
		if ([string]$record.sha256 -cne (Get-ReleaseFileSha256 -Path $actual.FullName) -or
			[int64]$record.size -ne [int64]$actual.Length) {
			throw "MSI payload verification record '$relativePath' does not match the qualified update bytes."
		}
	}
} finally {
	Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$administrativeArtifacts = @(Assert-ObjectProperty -Object $verification `
	-Name 'allowedAdministrativeArtifacts' -Context 'MSI payload verification')
if ([int]$verification.allowedAdministrativeArtifactCount -ne $administrativeArtifacts.Count) {
	throw "MSI payload verification administrative-artifact count is inconsistent."
}
foreach ($record in $administrativeArtifacts) {
	Assert-ExactProperties -Object $record -Context 'Allowed MSI administrative artifact' `
		-Names @('path', 'kind', 'sha256', 'size')
	$path = [string]$record.path
	$kind = [string]$record.kind
	$expectedExtension = if ($kind -ceq 'administrative-database') {
		'.msi'
	} elseif ($kind -ceq 'external-cabinet') {
		'.cab'
	} else {
		''
	}
	if ([string]::IsNullOrWhiteSpace($expectedExtension) -or $path.Contains('/') -or $path.Contains('\') -or
		[IO.Path]::GetExtension($path) -cne $expectedExtension -or
		[string]$record.sha256 -cnotmatch '^[0-9a-f]{64}$' -or [int64]$record.size -le 0) {
		throw "MSI payload verification contains an undocumented administrative artifact '$path'."
	}
}

Write-Host "Verified signed MSI administrative-extraction attestation against the complete update payload."
