[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$CandidatePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')]
	[string]$ExpectedCandidateExecutableSha256,
	[Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int]$ExpectedBuildNumber,
	[Parameter(Mandatory = $true)] [string]$ExpectedPublicKeyHex,
	[Parameter(Mandatory = $true)] [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

$candidate = Get-Item -LiteralPath $CandidatePath -Force -ErrorAction Stop
if ($candidate.PSIsContainer -or ($candidate.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
	$candidate.Name -ine 'mumble.exe') {
	throw 'Embedded-key attestation requires the regular staged mumble.exe file.'
}
$candidateSha256 = Get-ReleaseFileSha256 -Path $candidate.FullName
if ($candidateSha256 -cne $ExpectedCandidateExecutableSha256) {
	throw 'Embedded-key attestation candidate does not match the expected executable SHA-256.'
}
$null = Assert-Ed25519PublicKeyHex -PublicKeyHex $ExpectedPublicKeyHex

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('mumble-embedded-key-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot -ErrorAction Stop | Out-Null
$diagnosticPath = Join-Path $temporaryRoot 'runtime-build-identity.json'
try {
	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $candidate.FullName
	$startInfo.Arguments = '--write-input-enhancement-build-identity'
	$startInfo.WorkingDirectory = $candidate.DirectoryName
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.EnvironmentVariables['MUMBLE_INPUT_ENHANCEMENT_IDENTITY_OUTPUT'] = $diagnosticPath
	$process = [Diagnostics.Process]::Start($startInfo)
	if (-not $process) { throw 'Unable to launch the staged candidate build-identity diagnostic.' }
	try {
		if (-not $process.WaitForExit(30000)) {
			$process.Kill()
			throw 'Staged candidate build-identity diagnostic timed out.'
		}
		if ($process.ExitCode -ne 0) {
			throw "Staged candidate build-identity diagnostic failed with exit code $($process.ExitCode)."
		}
	} finally {
		$process.Dispose()
	}

	$diagnostic = Get-Item -LiteralPath $diagnosticPath -Force -ErrorAction Stop
	if ($diagnostic.PSIsContainer -or ($diagnostic.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
		$diagnostic.Length -le 0 -or $diagnostic.Length -gt 4096) {
		throw 'Staged candidate emitted an invalid build-identity diagnostic file.'
	}
	[byte[]]$diagnosticBytes = [IO.File]::ReadAllBytes($diagnostic.FullName)
	$attestation = [ordered]@{
		schemaVersion = 1
		kind = 'input-enhancement-embedded-key-attestation'
		passed = $true
		audioFree = $true
		createdAtUtc = [DateTimeOffset]::UtcNow.ToString('o')
		candidateExecutableSha256 = $candidateSha256
		generatorSha256 = Get-ReleaseFileSha256 -Path $PSCommandPath
		runtimeDiagnosticSha256 = Get-ReleaseFileSha256 -Path $diagnostic.FullName
		runtimeDiagnosticBase64 = [Convert]::ToBase64String($diagnosticBytes)
	}
	$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
	$outputParent = [IO.Path]::GetDirectoryName($outputFullPath)
	if ([string]::IsNullOrWhiteSpace($outputParent)) { throw 'Embedded-key attestation output has no parent.' }
	New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
	$temporaryOutput = Join-Path $outputParent ('.' + [IO.Path]::GetFileName($outputFullPath) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
	try {
		Write-ReleaseJson -Path $temporaryOutput -Value $attestation
		& (Join-Path $PSScriptRoot 'assert-input-enhancement-embedded-key-attestation.ps1') `
			-EvidencePath $temporaryOutput `
			-ExpectedCandidateExecutableSha256 $ExpectedCandidateExecutableSha256 `
			-ExpectedBuildNumber $ExpectedBuildNumber `
			-ExpectedPublicKeyHex $ExpectedPublicKeyHex
		Move-Item -LiteralPath $temporaryOutput -Destination $outputFullPath -Force
	} finally {
		Remove-Item -LiteralPath $temporaryOutput -Force -ErrorAction SilentlyContinue
	}
} finally {
	Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Created embedded-key attestation '$OutputPath' for the exact staged candidate."
