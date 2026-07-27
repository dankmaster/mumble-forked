[CmdletBinding()]
param(
	[string]$EnvironmentDir = "",
	[string]$EnvironmentRelease = "",
	[string]$EnvironmentCommit = "",
	[string]$EnvironmentVersionSuffix = "",
	[ValidateSet("shared", "static")]
	[string]$BuildType = "shared",
	[ValidateSet("x86_64")]
	[string]$Architecture = "x86_64",
	[string]$VolumeSize = "1900m",
	[ValidateRange(1, 9)]
	[int]$CompressionLevel = 5,
	[string]$OutputDirectory = "",
	[string]$ReleaseTag = "",
	[string]$Repository = "",
	[switch]$Upload,
	[switch]$CreateRelease,
	[switch]$Clobber,
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-SevenZipExecutable {
	$commandCandidates = @(
		(Get-Command 7z.exe -ErrorAction SilentlyContinue),
		(Get-Command 7za.exe -ErrorAction SilentlyContinue)
	) | Where-Object { $_ }

	foreach ($candidate in $commandCandidates) {
		return $candidate.Source
	}

	$pathCandidates = @(
		"C:\Program Files\7-Zip\7z.exe",
		"C:\Program Files (x86)\7-Zip\7z.exe",
		"C:\Program Files\PeaZip\res\bin\7z\7z.exe",
		"C:\Program Files (x86)\PeaZip\res\bin\7z\7z.exe",
		"C:\Apps\7-Zip\7z.exe",
		"C:\Apps\7-Zip\7za.exe",
		"C:\Apps\VMware\VMware Workstation\7za.exe"
	)

	foreach ($candidate in $pathCandidates) {
		if (Test-Path -LiteralPath $candidate) {
			return $candidate
		}
	}

	throw "Unable to locate 7z.exe or 7za.exe. Install 7-Zip or PeaZip, or add a 7-Zip-compatible CLI to PATH."
}

function Get-GitHubCliExecutable {
	$gh = Get-Command gh.exe -ErrorAction SilentlyContinue
	if ($gh) {
		return $gh.Source
	}

	throw "Unable to locate gh.exe. Install GitHub CLI before attempting release uploads."
}

function Get-OriginRepository {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot
	)

	$originUrl = (& git -C $RepoRoot remote get-url origin).Trim()
	if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($originUrl)) {
		throw "Unable to determine the origin remote URL for '$RepoRoot'."
	}

	if ($originUrl -match 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/]+?)(?:\.git)?$') {
		return "$($Matches.owner)/$($Matches.repo)"
	}

	throw "The origin remote does not look like a GitHub repository URL: '$originUrl'"
}

function Resolve-EnvironmentMetadata {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot,

		[string]$EnvironmentDir,
		[string]$EnvironmentRelease,
		[string]$EnvironmentCommit,
		[string]$BuildType
	)

	$resolvedBuildType = $BuildType
	$resolvedRelease = $EnvironmentRelease
	$resolvedCommit = $EnvironmentCommit
	$resolvedEnvironmentDir = $EnvironmentDir

	if ([string]::IsNullOrWhiteSpace($resolvedEnvironmentDir)) {
		if ([string]::IsNullOrWhiteSpace($resolvedRelease) -or [string]::IsNullOrWhiteSpace($resolvedCommit)) {
			throw "Specify -EnvironmentDir or both -EnvironmentRelease and -EnvironmentCommit."
		}

		$resolvedEnvironmentDir = Join-Path $RepoRoot "build_env\$resolvedRelease-$resolvedCommit-$resolvedBuildType"
	}

	$resolvedEnvironmentDir = [System.IO.Path]::GetFullPath($resolvedEnvironmentDir)
	$leafName = Split-Path -Path $resolvedEnvironmentDir -Leaf

	if ($leafName -match '^(?<release>.+)-(?<commit>[0-9a-f]{10})(?:-(?<kind>shared|static))?$') {
		if ([string]::IsNullOrWhiteSpace($resolvedRelease)) {
			$resolvedRelease = $Matches.release
		}
		if ([string]::IsNullOrWhiteSpace($resolvedCommit)) {
			$resolvedCommit = $Matches.commit
		}
		if ($Matches.kind) {
			$resolvedBuildType = $Matches.kind
		}
	}

	if ([string]::IsNullOrWhiteSpace($resolvedRelease) -or [string]::IsNullOrWhiteSpace($resolvedCommit)) {
		throw "Unable to determine the environment release/commit from '$resolvedEnvironmentDir'. Pass them explicitly."
	}

	return [PSCustomObject]@{
		EnvironmentDir = $resolvedEnvironmentDir
		EnvironmentRelease = $resolvedRelease
		EnvironmentCommit = $resolvedCommit
		BuildType = $resolvedBuildType
	}
}

function Get-VcpkgTriplet {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Architecture,

		[Parameter(Mandatory = $true)]
		[string]$BuildType
	)

	switch ("$Architecture/$BuildType") {
		"x86_64/shared" { return "x64-windows" }
		"x86_64/static" { return "x64-windows-static-md" }
		default {
			throw "Unsupported build environment combination: architecture='$Architecture', buildType='$BuildType'."
		}
	}
}

function Get-CMakeTargetFeatureMap {
	param(
		[Parameter(Mandatory = $true)]
		[string]$TargetsText,

		[Parameter(Mandatory = $true)]
		[string]$PropertyName
	)

	$pattern = [regex]::Escape($PropertyName) + '\s+"([^"]*)"'
	$match = [regex]::Match($TargetsText, $pattern)
	if (-not $match.Success) {
		return $null
	}

	$features = @{}
	foreach ($feature in ($match.Groups[1].Value -split ';')) {
		if (-not [string]::IsNullOrWhiteSpace($feature)) {
			$features[$feature] = $true
		}
	}

	return $features
}

function Test-CMakeTargetFeatureEnabled {
	param(
		[Parameter(Mandatory = $true)]
		[string]$TargetsText,

		[Parameter(Mandatory = $true)]
		[string]$EnabledProperty,

		[Parameter(Mandatory = $true)]
		[string]$DisabledProperty,

		[Parameter(Mandatory = $true)]
		[string]$Feature
	)

	$enabledFeatures = Get-CMakeTargetFeatureMap -TargetsText $TargetsText -PropertyName $EnabledProperty
	$disabledFeatures = Get-CMakeTargetFeatureMap -TargetsText $TargetsText -PropertyName $DisabledProperty
	if ($null -eq $enabledFeatures -or $null -eq $disabledFeatures) {
		return $false
	}

	return $enabledFeatures.ContainsKey($Feature) -and (-not $disabledFeatures.ContainsKey($Feature))
}

function Assert-QtWebEngineFeature {
	param(
		[Parameter(Mandatory = $true)]
		[string]$TargetsText,

		[Parameter(Mandatory = $true)]
		[string]$EnabledProperty,

		[Parameter(Mandatory = $true)]
		[string]$DisabledProperty,

		[Parameter(Mandatory = $true)]
		[string]$Feature,

		[Parameter(Mandatory = $true)]
		[string]$TargetsPath
	)

	if (-not (Test-CMakeTargetFeatureEnabled `
		-TargetsText $TargetsText `
		-EnabledProperty $EnabledProperty `
		-DisabledProperty $DisabledProperty `
		-Feature $Feature)) {
		throw "Qt WebEngine feature '$Feature' is not enabled in '$TargetsPath'. Rebuild the shared environment with the required WebEngine feature set before publishing it."
	}
}

function Copy-DirectoryContents {
	param(
		[Parameter(Mandatory = $true)]
		[string]$SourceDir,

		[Parameter(Mandatory = $true)]
		[string]$DestinationDir
	)

	New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
	Get-ChildItem -LiteralPath $SourceDir -Force | ForEach-Object {
		Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Recurse -Force
	}
}

function Ensure-QtWebEngineDeployResourceLayout {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentDir,

		[Parameter(Mandatory = $true)]
		[string]$Triplet
	)

	$tripletRoot = Join-Path $EnvironmentDir "installed\$Triplet"
	$resourceMirrors = @(
		@{
			Source = Join-Path $tripletRoot "resources"
			Destination = Join-Path $tripletRoot "share\Qt6\resources"
			Probe = "icudtl.dat"
		},
		@{
			Source = Join-Path $tripletRoot "debug\resources"
			Destination = Join-Path $tripletRoot "debug\share\Qt6\resources"
			Probe = "icudtl.dat"
		}
	)

	foreach ($mirror in $resourceMirrors) {
		if (-not (Test-Path -LiteralPath $mirror.Source)) {
			continue
		}

		$probePath = Join-Path $mirror.Destination $mirror.Probe
		if (Test-Path -LiteralPath $probePath) {
			continue
		}

		Write-Host "Mirroring Qt WebEngine resources from '$($mirror.Source)' to '$($mirror.Destination)' for windeployqt."
		Copy-DirectoryContents -SourceDir $mirror.Source -DestinationDir $mirror.Destination
	}
}

function Assert-EnvironmentLooksReady {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentDir,

		[Parameter(Mandatory = $true)]
		[string]$Triplet,

		[Parameter(Mandatory = $true)]
		[string]$BuildType
	)

	if ($BuildType -eq "shared") {
		Ensure-QtWebEngineDeployResourceLayout -EnvironmentDir $EnvironmentDir -Triplet $Triplet
	}

	$requiredPaths = @(
		(Join-Path $EnvironmentDir "vcpkg.exe"),
		(Join-Path $EnvironmentDir "scripts\buildsystems\vcpkg.cmake"),
		(Join-Path $EnvironmentDir "installed\$Triplet")
	)

	if ($BuildType -eq "shared") {
		$requiredPaths += @(
			(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6WebEngineCore\Qt6WebEngineCoreTargets.cmake"),
			(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6Multimedia\Qt6MultimediaTargets.cmake"),
			(Join-Path $EnvironmentDir "installed\$Triplet\bin\Qt6Multimedia.dll"),
			(Join-Path $EnvironmentDir "installed\$Triplet\bin\Qt6MultimediaQuick.dll"),
			(Join-Path $EnvironmentDir "installed\$Triplet\Qt6\qml\QtMultimedia\quickmultimediaplugin.dll"),
			(Join-Path $EnvironmentDir "installed\$Triplet\tools\Qt6\bin\windeployqt.exe"),
			(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6\resources\icudtl.dat"),
			(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6\resources\qtwebengine_resources.pak"),
			(Join-Path $EnvironmentDir "installed\x86-windows")
		)
	}

	$missing = @($requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_) })
	if ($missing.Count -gt 0) {
		throw "The build environment under '$EnvironmentDir' is missing required content: $($missing -join ', ')"
	}

	if ($BuildType -eq "shared") {
		$x86TripletPath = Join-Path $EnvironmentDir "installed\x86-windows"
		$x86TripletContent = Get-ChildItem -LiteralPath $x86TripletPath -Force -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($null -eq $x86TripletContent) {
			throw "The shared build environment under '$EnvironmentDir' has an empty x86-windows triplet. Rebuild or repair the environment before publishing it."
		}

		$webengineTargetsPath = Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6WebEngineCore\Qt6WebEngineCoreTargets.cmake"
		$webengineTargetsText = Get-Content -Raw -LiteralPath $webengineTargetsPath

		Assert-QtWebEngineFeature `
			-TargetsText $webengineTargetsText `
			-EnabledProperty "QT_ENABLED_PRIVATE_FEATURES" `
			-DisabledProperty "QT_DISABLED_PRIVATE_FEATURES" `
			-Feature "webengine_proprietary_codecs" `
			-TargetsPath $webengineTargetsPath
	}
}

function New-ReleaseNotes {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentRelease,

		[Parameter(Mandatory = $true)]
		[string]$EnvironmentCommit,

		[string]$EnvironmentVersionSuffix = ""
	)

	$lines = @(
		"Prebuilt Windows build environment archives for environment release $EnvironmentRelease.",
		"",
		"- Environment commit: $EnvironmentCommit"
	)
	if (-not [string]::IsNullOrWhiteSpace($EnvironmentVersionSuffix)) {
		$lines += "- Environment version suffix: $EnvironmentVersionSuffix"
	}
	$lines += @(
		"- Shared Windows environments are expected to include Qt WebEngine proprietary codecs",
		"- Shared Windows environments are expected to include the Qt Multimedia Quick/QML runtime",
		"- Generated from a local build_env checkout",
		"- Intended for GitHub Actions release-asset reuse"
	)

	return ($lines -join "`n")
}

function Test-GitHubReleaseExists {
	param(
		[Parameter(Mandatory = $true)]
		[string]$GitHubCli,

		[Parameter(Mandatory = $true)]
		[string]$Tag,

		[Parameter(Mandatory = $true)]
		[string]$Repository
	)

	# Windows PowerShell can surface native stderr text as an error record when a
	# release does not exist yet. Redirect stderr explicitly so we can inspect
	# the exit code and create the release in the normal path.
	& $GitHubCli release view $Tag --repo $Repository 2>$null | Out-Null
	return ($LASTEXITCODE -eq 0)
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path

$metadata = Resolve-EnvironmentMetadata `
	-RepoRoot $repoRoot `
	-EnvironmentDir $EnvironmentDir `
	-EnvironmentRelease $EnvironmentRelease `
	-EnvironmentCommit $EnvironmentCommit `
	-BuildType $BuildType

$triplet = Get-VcpkgTriplet -Architecture $Architecture -BuildType $metadata.BuildType
Assert-EnvironmentLooksReady -EnvironmentDir $metadata.EnvironmentDir -Triplet $triplet -BuildType $metadata.BuildType

if (-not [string]::IsNullOrWhiteSpace($EnvironmentVersionSuffix) -and $EnvironmentVersionSuffix -notmatch '^[A-Za-z0-9._-]+$') {
	throw "EnvironmentVersionSuffix may only contain letters, numbers, '.', '_' and '-'."
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path $repoRoot ".tmp\build-env-archives"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$assetVersion = "mumble_env.$triplet.$($metadata.EnvironmentCommit)"
if (-not [string]::IsNullOrWhiteSpace($EnvironmentVersionSuffix)) {
	$assetVersion = "$assetVersion.$EnvironmentVersionSuffix"
}
$assetName = "$assetVersion.7z"
$archivePath = Join-Path $OutputDirectory $assetName
$releaseTagToUse = if ([string]::IsNullOrWhiteSpace($ReleaseTag)) {
	"build-env-$($metadata.EnvironmentRelease)"
} else {
	$ReleaseTag
}

$repositoryToUse = $Repository
if ($Upload -and [string]::IsNullOrWhiteSpace($repositoryToUse)) {
	$repositoryToUse = Get-OriginRepository -RepoRoot $repoRoot
}

$existingArchiveParts = @(Get-ChildItem -Path "$archivePath*" -File -ErrorAction SilentlyContinue)
if ($existingArchiveParts.Count -gt 0) {
	if (-not $Clobber) {
		throw "Archive output already exists at '$archivePath*'. Pass -Clobber to replace it."
	}

	$existingArchiveParts | Remove-Item -Force
}

Write-Host "Build environment source: $($metadata.EnvironmentDir)"
Write-Host "Resolved environment release: $($metadata.EnvironmentRelease)"
Write-Host "Resolved environment commit: $($metadata.EnvironmentCommit)"
if (-not [string]::IsNullOrWhiteSpace($EnvironmentVersionSuffix)) {
	Write-Host "Resolved environment version suffix: $EnvironmentVersionSuffix"
}
Write-Host "Resolved build type: $($metadata.BuildType)"
Write-Host "Triplet: $triplet"
Write-Host "Archive path: $archivePath"
Write-Host "Volume size: $VolumeSize"
Write-Host "Compression level: $CompressionLevel"
if ($Upload) {
	Write-Host "GitHub repository: $repositoryToUse"
	Write-Host "GitHub release tag: $releaseTagToUse"
}

if ($DryRun) {
	Write-Host "Dry run requested; skipping archive creation and release upload."
	return
}

$sevenZip = Get-SevenZipExecutable
$environmentParent = Split-Path -Parent $metadata.EnvironmentDir
$environmentLeaf = Split-Path -Leaf $metadata.EnvironmentDir

Push-Location $environmentParent
try {
	$sevenZipArgs = @("a", "-t7z", "-mx=$CompressionLevel", "-mmt=on")
	if (-not [string]::IsNullOrWhiteSpace($VolumeSize)) {
		$sevenZipArgs += "-v$VolumeSize"
	}
	$sevenZipArgs += @($archivePath, $environmentLeaf)

	& $sevenZip @sevenZipArgs
	if ($LASTEXITCODE -ne 0) {
		throw "7-Zip failed while creating '$archivePath'."
	}
}
finally {
	Pop-Location
}

$archiveParts = @(Get-ChildItem -Path "$archivePath*" -File -ErrorAction Stop | Sort-Object -Property Name)
if ($archiveParts.Count -eq 0) {
	throw "7-Zip completed, but no archive files were produced for '$archivePath'."
}

$totalBytes = ($archiveParts | Measure-Object -Property Length -Sum).Sum
Write-Host ("Archive parts: {0}" -f ($archiveParts.Name -join ", "))
Write-Host ("Total archive size: {0:N2} GB" -f ($totalBytes / 1GB))
$archiveParts | ForEach-Object {
	$archiveHash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
	Write-Host ("{0} SHA256: {1}" -f $_.Name, $archiveHash.Hash)
}

if (-not $Upload) {
	return
}

$gh = Get-GitHubCliExecutable
& $gh auth status | Out-Null
if ($LASTEXITCODE -ne 0) {
	throw "GitHub CLI is not authenticated. Run 'gh auth login' before using -Upload."
}

$releaseExists = Test-GitHubReleaseExists -GitHubCli $gh -Tag $releaseTagToUse -Repository $repositoryToUse
if (-not $releaseExists) {
	if (-not $CreateRelease) {
		throw "Release '$releaseTagToUse' does not exist in '$repositoryToUse'. Pass -CreateRelease to create it."
	}

	$releaseNotes = New-ReleaseNotes `
		-EnvironmentRelease $metadata.EnvironmentRelease `
		-EnvironmentCommit $metadata.EnvironmentCommit `
		-EnvironmentVersionSuffix $EnvironmentVersionSuffix
	$releaseCreateArgs = @(
		"release", "create", $releaseTagToUse,
		"--repo", $repositoryToUse,
		"--title", "Build environment $($metadata.EnvironmentRelease)",
		"--notes", $releaseNotes
	) + @($archiveParts.FullName)
	& $gh @releaseCreateArgs
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to create release '$releaseTagToUse' in '$repositoryToUse'."
	}
} else {
	$uploadArgs = @("release", "upload", $releaseTagToUse, "--repo", $repositoryToUse) + @($archiveParts.FullName)
	if ($Clobber) {
		$uploadArgs += "--clobber"
	}

	& $gh @uploadArgs
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to upload '$archivePath' to release '$releaseTagToUse' in '$repositoryToUse'."
	}
}

Write-Host "Published $assetName to https://github.com/$repositoryToUse/releases/tag/$releaseTagToUse"
