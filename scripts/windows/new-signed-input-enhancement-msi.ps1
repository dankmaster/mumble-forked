[CmdletBinding()]
param(
	[string]$RepoRoot = ".",

	[Parameter(Mandatory = $true)]
	[string]$BuildRoot,

	[Parameter(Mandatory = $true)]
	[string]$StageRoot,

	[Parameter(Mandatory = $true)]
	[string]$ExpectedSignerSubject,

	[Parameter(Mandatory = $true)]
	[string]$OutputPath,

	[Parameter(Mandatory = $true)]
	[string]$PayloadSignatureResultsPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-CMakeCacheValue {
	param(
		[Parameter(Mandatory = $true)]
		[string]$CachePath,
		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	foreach ($line in Get-Content -LiteralPath $CachePath) {
		if ($line -match "^$([regex]::Escape($Name)):[^=]+=(?<value>.*)$") {
			return $Matches.value.Trim()
		}
	}
	return ""
}

function Get-CscsExecutable {
	$command = Get-Command cscs.exe -ErrorAction SilentlyContinue
	if ($command) {
		return $command.Source
	}
	if (Test-Path -LiteralPath "C:\WixSharp\cscs.exe" -PathType Leaf) {
		return "C:\WixSharp\cscs.exe"
	}
	throw "Unable to locate cscs.exe. Run the Windows installer tooling setup before rebuilding the signed MSI."
}

foreach ($entry in @(
	@("RepoRoot", $RepoRoot),
	@("BuildRoot", $BuildRoot),
	@("StageRoot", $StageRoot)
)) {
	if (-not (Test-Path -LiteralPath ([string]$entry[1]) -PathType Container)) {
		throw "$($entry[0]) does not exist: '$($entry[1])'."
	}
}
$repoRootPath = (Resolve-Path -LiteralPath $RepoRoot).Path
$buildRootPath = (Resolve-Path -LiteralPath $BuildRoot).Path
$stageRootPath = (Resolve-Path -LiteralPath $StageRoot).Path
$cachePath = Join-Path $buildRootPath "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
	throw "Signed MSI rebuild requires '$cachePath'."
}

# This is intentionally before invoking the installer compiler. The MSI must be
# constructed from already signed and timestamped payload bytes, not merely have
# an Authenticode signature added to a container of unsigned executables.
& (Join-Path $PSScriptRoot "assert-input-enhancement-signatures.ps1") `
	-Root $stageRootPath `
	-ExpectedSignerSubject $ExpectedSignerSubject `
	-OutputPath $PayloadSignatureResultsPath

$vcRedistVersion = Get-CMakeCacheValue -CachePath $cachePath -Name "VC_REDIST_VERSION"
if ([string]::IsNullOrWhiteSpace($vcRedistVersion)) {
	$vcRedistInstaller = Join-Path $buildRootPath "installer\VC_redist.x64.exe"
	if (Test-Path -LiteralPath $vcRedistInstaller -PathType Leaf) {
		$vcRedistVersion = (Get-Item -LiteralPath $vcRedistInstaller).VersionInfo.ProductVersion
	}
}
if ([string]::IsNullOrWhiteSpace($vcRedistVersion)) {
	throw "Unable to determine VC_REDIST_VERSION for the signed MSI rebuild."
}

$projectVersion = (& python (Join-Path $repoRootPath "scripts\mumble-version.py")).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($projectVersion)) {
	throw "Unable to determine Mumble project version."
}
if ($projectVersion -match '^\d+\.\d+$') {
	$projectVersion = "$projectVersion.0"
}
$installerVersion = Get-CMakeCacheValue -CachePath $cachePath -Name "MUMBLE_WINDOWS_INSTALLER_VERSION"
if ([string]::IsNullOrWhiteSpace($installerVersion)) {
	$installerVersion = $projectVersion
}
if ($installerVersion -match '^\d+\.\d+$') {
	$installerVersion = "$installerVersion.0"
}

$installerWorkDir = Join-Path $buildRootPath "installer\client"
New-Item -ItemType Directory -Force -Path $installerWorkDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRootPath "installer\MumbleInstall.cs") -Destination $installerWorkDir -Force
Copy-Item -LiteralPath (Join-Path $repoRootPath "installer\ClientInstaller.cs") -Destination $installerWorkDir -Force
Get-ChildItem -LiteralPath $installerWorkDir -File -ErrorAction SilentlyContinue |
	Where-Object {
		$_.Name -like "*.msi" -or
		$_.Name -like "*.mst" -or
		$_.Name -like "*.wixobj" -or
		$_.Name -like "*.wxs" -or
		$_.Name -like "*_client-*.exe"
	} |
	Remove-Item -Force

$installerArgs = @(
	"--version", $projectVersion,
	"--installer-version", $installerVersion,
	"--arch", "x64",
	"--vc-redist-required", $vcRedistVersion,
	"--payload-root", $stageRootPath
)
if (Test-Path -LiteralPath (Join-Path $stageRootPath "rnnoise.dll") -PathType Leaf) {
	$installerArgs += "--rnnoise"
}
if (Test-Path -LiteralPath (Join-Path $stageRootPath "mumble-screen-helper.exe") -PathType Leaf) {
	$installerArgs += "--screen-share-helper"
}

$cscs = Get-CscsExecutable
Push-Location $installerWorkDir
try {
	& $cscs -cd MumbleInstall.cs
	if ($LASTEXITCODE -ne 0) {
		throw "cscs failed while compiling MumbleInstall.cs for the signed MSI."
	}
	& $cscs ClientInstaller.cs @installerArgs
	if ($LASTEXITCODE -ne 0) {
		throw "cscs failed while rebuilding the MSI from the signed staged payload."
	}
} finally {
	Pop-Location
}

$msis = @(Get-ChildItem -LiteralPath $installerWorkDir -File -Filter "*client*.msi")
if ($msis.Count -ne 1) {
	throw "Signed payload rebuild expected exactly one English client MSI, found $($msis.Count)."
}
$outputParent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputParent)) {
	New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
}
Copy-Item -LiteralPath $msis[0].FullName -Destination $OutputPath -Force
Write-Host "Rebuilt MSI '$OutputPath' from the verified signed staged payload."
