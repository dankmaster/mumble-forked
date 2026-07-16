[CmdletBinding()]
param(
	[string]$BuildRoot = ".\build",
	[string]$ArtifactListPath = "",
	[string]$StageRoot = "",
	[switch]$RequireStage,
	[switch]$RequireClient,
	[switch]$RequireServer,
	[switch]$RequireScreenHelper,
	[switch]$RequireClientInstaller,
	[switch]$RequireServerInstaller,
	[switch]$RequireEnglishOnlyInstallers,
	[switch]$RequireUpdaterRuntime,
	[switch]$RequireSpeechCleanup,
	[switch]$RequireGStreamerRuntime
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-ExistingPath {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	if (-not (Test-Path -LiteralPath $Path)) {
		throw "Required path does not exist: '$Path'."
	}

	return (Resolve-Path -LiteralPath $Path).Path
}

function Get-RequiredBinaryNames {
	$explicitSelection = $RequireClient.IsPresent -or $RequireServer.IsPresent -or $RequireScreenHelper.IsPresent
	if (-not $explicitSelection) {
		return @(
			"mumble.exe",
			"mumble-updater.exe",
			"mumble-server.exe",
			"mumble-screen-helper.exe"
		)
	}

	$required = New-Object System.Collections.Generic.List[string]
	if ($RequireClient) {
		$required.Add("mumble.exe")
		$required.Add("mumble-updater.exe")
	}
	if ($RequireServer) {
		$required.Add("mumble-server.exe")
	}
	if ($RequireScreenHelper) {
		$required.Add("mumble-screen-helper.exe")
	}

	if ($required.Count -eq 0) {
		throw "No required Windows binaries were selected for validation."
	}

	return $required
}

function Find-Binary {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string]$BinaryName
	)

	return Get-ChildItem -Path $Root -Recurse -File -Filter $BinaryName -ErrorAction SilentlyContinue |
		Sort-Object -Property FullName |
		Select-Object -First 1
}

function Assert-BinarySet {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string[]]$BinaryNames
	)

	$verifiedPaths = New-Object System.Collections.Generic.List[string]
	$missing = New-Object System.Collections.Generic.List[string]

	foreach ($binaryName in $BinaryNames) {
		$binary = Find-Binary -Root $Root -BinaryName $binaryName
		if ($binary) {
			$verifiedPaths.Add($binary.FullName)
		} else {
			$missing.Add($binaryName)
		}
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required Windows build outputs: $($missing -join ', ')."
	}

	Write-Host "$Label verified:"
	foreach ($path in $verifiedPaths) {
		Write-Host "  $path"
	}

	return $verifiedPaths
}

function Get-CMakeBooleanOption {
	param(
		[Parameter(Mandatory = $true)]
		[string[]]$CacheLines,

		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	$pattern = "^$([System.Text.RegularExpressions.Regex]::Escape($Name)):BOOL=(.*)$"
	$matches = @($CacheLines | Where-Object { $_ -match $pattern })
	if ($matches.Count -ne 1) {
		throw "CMake cache does not contain exactly one BOOL entry for '$Name'."
	}

	$value = [System.Text.RegularExpressions.Regex]::Match($matches[0], $pattern).Groups[1].Value.Trim()
	if ($value -match '^(?i:1|ON|TRUE|YES|Y)$') {
		return $true
	}
	if ($value -match '^(?i:0|OFF|FALSE|NO|N|IGNORE|.*-NOTFOUND)$') {
		return $false
	}

	throw "CMake cache option '$Name' has unsupported BOOL value '$value'."
}

function Get-SpeechCleanupFeatureSet {
	param(
		[Parameter(Mandatory = $true)]
		[string]$BuildRoot
	)

	$cachePath = Join-Path $BuildRoot "CMakeCache.txt"
	if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
		throw "Speech-cleanup payload validation requires a CMake cache at '$cachePath'."
	}

	$cacheLines = @(Get-Content -LiteralPath $cachePath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	$features = [pscustomobject]@{
		RNNoise        = Get-CMakeBooleanOption -CacheLines $cacheLines -Name "rnnoise"
		BundledRNNoise = Get-CMakeBooleanOption -CacheLines $cacheLines -Name "bundled-rnnoise"
		DTLN           = Get-CMakeBooleanOption -CacheLines $cacheLines -Name "dtln"
		DeepFilterNet  = Get-CMakeBooleanOption -CacheLines $cacheLines -Name "deepfilternet"
	}

	if (-not $features.RNNoise -and -not $features.DTLN -and -not $features.DeepFilterNet) {
		throw "Speech-cleanup payload validation was requested, but all speech-cleanup backends are disabled in '$cachePath'."
	}

	return $features
}

function Assert-SpeechCleanupPayload {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[pscustomobject]$Features
	)

	$requiredRelativePaths = New-Object System.Collections.Generic.List[string]
	$enabledBackendNames = New-Object System.Collections.Generic.List[string]

	if ($Features.RNNoise) {
		$enabledBackendNames.Add("RNNoise")
		if ($Features.BundledRNNoise) {
			# The bundled Windows RNNoise target is a shared library and also produces
			# the selectable little-model blob. External RNNoise packages may link
			# statically, so they intentionally have no generic payload requirement.
			$requiredRelativePaths.Add("rnnoise.dll")
			$requiredRelativePaths.Add("rnnoise\rnnoise_little.weights_blob.bin")
		}
	}

	if ($Features.DTLN) {
		$enabledBackendNames.Add("DTLN")
		$requiredRelativePaths.Add("onnxruntime.dll")
		# DTLN uses the SpeexDSP resampler at runtime. Keep this explicit so a
		# staged DLL that predates a newly exported resampler API cannot survive
		# packaging and fail only when the client starts.
		$requiredRelativePaths.Add("speexdsp.dll")
		foreach ($variant in @("baseline", "norm_500h", "norm_40h")) {
			$requiredRelativePaths.Add("dtln\$variant\model_1.onnx")
			$requiredRelativePaths.Add("dtln\$variant\model_2.onnx")
		}
	}

	if ($Features.DeepFilterNet) {
		$enabledBackendNames.Add("DeepFilterNet")
		$requiredRelativePaths.Add("deepfilter.dll")
		$requiredRelativePaths.Add("deepfilternet\DeepFilterNet3_onnx.tar.gz")
		$requiredRelativePaths.Add("deepfilternet\DeepFilterNet3_ll_onnx.tar.gz")
	}

	$missing = New-Object System.Collections.Generic.List[string]
	$empty = New-Object System.Collections.Generic.List[string]
	foreach ($relativePath in $requiredRelativePaths) {
		$fullPath = Join-Path $Root $relativePath
		if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
			$missing.Add($relativePath)
		} elseif ((Get-Item -LiteralPath $fullPath).Length -eq 0) {
			$empty.Add($relativePath)
		}
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required speech-cleanup payload files: $($missing -join ', ')."
	}
	if ($empty.Count -gt 0) {
		throw "$Label contains empty required speech-cleanup payload files: $($empty -join ', ')."
	}

	Write-Host "$Label speech-cleanup payload verified for: $($enabledBackendNames -join ', ')."
}

function Assert-MatchingPayloadFile {
	param(
		[Parameter(Mandatory = $true)]
		[string]$BuildRoot,

		[Parameter(Mandatory = $true)]
		[string]$StageRoot,

		[Parameter(Mandatory = $true)]
		[string]$RelativePath
	)

	$buildPath = Join-Path $BuildRoot $RelativePath
	$stagePath = Join-Path $StageRoot $RelativePath
	$buildHash = (Get-FileHash -LiteralPath $buildPath -Algorithm SHA256).Hash
	$stageHash = (Get-FileHash -LiteralPath $stagePath -Algorithm SHA256).Hash
	if ($buildHash -ne $stageHash) {
		throw "Stage payload '$RelativePath' does not match the build output (build SHA256: $buildHash; stage SHA256: $stageHash)."
	}

	Write-Host "Stage payload '$RelativePath' matches the build output."
}

function Assert-UpdaterRuntimePayload {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$updater = Find-Binary -Root $Root -BinaryName 'mumble-updater.exe'
	if (-not $updater) {
		throw "$Label is missing mumble-updater.exe."
	}
	& (Join-Path $PSScriptRoot 'assert-mumble-updater-static-runtime.ps1') -UpdaterPath $updater.FullName
	Write-Host "$Label self-contained updater runtime verified."
}

function Assert-GStreamerPayload {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$requiredRelativePaths = @(
		"gstreamer\bin\gst-launch-1.0.exe",
		"gstreamer\bin\gst-inspect-1.0.exe",
		"gstreamer\lib\gstreamer-1.0",
		"gstreamer\libexec\gstreamer-1.0\gst-plugin-scanner.exe"
	)

	$missing = New-Object System.Collections.Generic.List[string]
	foreach ($relativePath in $requiredRelativePaths) {
		$fullPath = Join-Path $Root $relativePath
		if (-not (Test-Path -LiteralPath $fullPath)) {
			$missing.Add($relativePath)
		}
	}

	$pluginFiles = @(Get-ChildItem -Path (Join-Path $Root "gstreamer\lib\gstreamer-1.0") -File -Filter "*.dll" -ErrorAction SilentlyContinue)
	if ($pluginFiles.Count -eq 0) {
		$missing.Add("gstreamer\lib\gstreamer-1.0\*.dll")
	}

	# The screen-share helper's window-follow capture mode loads these core libraries directly
	# (LoadLibrary), not just by exec'ing gst-launch, so they must be present in the staged bin.
	$gstBinDir = Join-Path $Root "gstreamer\bin"
	foreach ($corePattern in @("gstreamer-1.0-*.dll", "gobject-2.0-*.dll", "glib-2.0-*.dll")) {
		$coreMatches = @(Get-ChildItem -Path $gstBinDir -File -Filter $corePattern -ErrorAction SilentlyContinue)
		if ($coreMatches.Count -eq 0) {
			$missing.Add("gstreamer\bin\$corePattern")
		}
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required GStreamer payload files: $($missing -join ', ')."
	}

	Write-Host "$Label GStreamer runtime payload verified."
}

function Get-ArtifactPaths {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	return Get-ChildItem -Path $Root -Recurse -File -Include "mumble*.exe", "mumble*.msi" -ErrorAction SilentlyContinue |
		Sort-Object -Property FullName |
		ForEach-Object { $_.FullName }
}

function Assert-InstallerPattern {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string]$Pattern,

		[Parameter(Mandatory = $true)]
		[string]$Description
	)

	$matches = @(Get-ChildItem -Path $Root -Recurse -File -Filter $Pattern -ErrorAction SilentlyContinue |
		Sort-Object -Property FullName)
	if ($matches.Count -eq 0) {
		throw "$Label is missing required Windows installer output: $Description ($Pattern)."
	}

	Write-Host "$Label installer output verified ($Description):"
	foreach ($match in $matches) {
		Write-Host "  $($match.FullName)"
	}

	return $matches.FullName
}

function Assert-EnglishOnlyInstallers {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$localizedArtifacts = @(Get-ChildItem -Path $Root -Recurse -File -Include "*.msi", "*.mst" -ErrorAction SilentlyContinue |
		Where-Object {
			$_.Extension -ieq ".mst" -or $_.BaseName -match '^[a-z]{2}-[A-Z]{2}$'
		} |
		Sort-Object -Property FullName)

	if ($localizedArtifacts.Count -gt 0) {
		$paths = $localizedArtifacts | ForEach-Object { $_.FullName }
		throw "$Label contains localized Windows installer byproducts, but English-only installers are required: $($paths -join ', ')."
	}

	Write-Host "$Label English-only installer output verified."
}

$buildRootPath = Resolve-ExistingPath -Path $BuildRoot
$allArtifacts = New-Object System.Collections.Generic.List[string]
$requiredBinaryNames = Get-RequiredBinaryNames
$speechCleanupFeatures = if ($RequireSpeechCleanup) {
	Get-SpeechCleanupFeatureSet -BuildRoot $buildRootPath
} else {
	$null
}

foreach ($verifiedPath in Assert-BinarySet -Label "Build root '$buildRootPath'" -Root $buildRootPath -BinaryNames $requiredBinaryNames) {
	$allArtifacts.Add($verifiedPath)
}

if ($RequireSpeechCleanup) {
	Assert-SpeechCleanupPayload -Label "Build root '$buildRootPath'" -Root $buildRootPath -Features $speechCleanupFeatures
}
if ($RequireUpdaterRuntime) {
	Assert-UpdaterRuntimePayload -Label "Build root '$buildRootPath'" -Root $buildRootPath
}
if ($RequireGStreamerRuntime -and -not $RequireStage) {
	Assert-GStreamerPayload -Label "Build root '$buildRootPath'" -Root $buildRootPath
}

foreach ($artifactPath in Get-ArtifactPaths -Root $buildRootPath) {
	$allArtifacts.Add($artifactPath)
}

if ($RequireClientInstaller) {
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*_client-*.exe" -Description "client installer bootstrapper") {
		$allArtifacts.Add($artifactPath)
	}
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*client*.msi" -Description "client installer MSI") {
		$allArtifacts.Add($artifactPath)
	}
}

if ($RequireServerInstaller) {
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*_server-*.exe" -Description "server installer bootstrapper") {
		$allArtifacts.Add($artifactPath)
	}
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*server*.msi" -Description "server installer MSI") {
		$allArtifacts.Add($artifactPath)
	}
}

if ($RequireEnglishOnlyInstallers) {
	Assert-EnglishOnlyInstallers -Label "Build root '$buildRootPath'" -Root $buildRootPath
}

if ($RequireStage) {
	if ([string]::IsNullOrWhiteSpace($StageRoot)) {
		throw "RequireStage was specified but no StageRoot was provided."
	}

	$stageRootPath = Resolve-ExistingPath -Path $StageRoot
	foreach ($verifiedPath in Assert-BinarySet -Label "Stage root '$stageRootPath'" -Root $stageRootPath -BinaryNames $requiredBinaryNames) {
		$allArtifacts.Add($verifiedPath)
	}
	if ($RequireSpeechCleanup) {
		Assert-SpeechCleanupPayload -Label "Stage root '$stageRootPath'" -Root $stageRootPath -Features $speechCleanupFeatures
		if ($speechCleanupFeatures.DTLN) {
			Assert-MatchingPayloadFile -BuildRoot $buildRootPath -StageRoot $stageRootPath -RelativePath "speexdsp.dll"
		}
	}
	if ($RequireUpdaterRuntime) {
		Assert-UpdaterRuntimePayload -Label "Stage root '$stageRootPath'" -Root $stageRootPath
	}
	if ($RequireGStreamerRuntime) {
		Assert-GStreamerPayload -Label "Stage root '$stageRootPath'" -Root $stageRootPath
	}
	foreach ($artifactPath in Get-ArtifactPaths -Root $stageRootPath) {
		$allArtifacts.Add($artifactPath)
	}
}

$uniqueArtifacts = @($allArtifacts | Sort-Object -Unique)
if ($uniqueArtifacts.Count -eq 0) {
	throw "No Windows build artifacts were found under '$buildRootPath'."
}

if (-not [string]::IsNullOrWhiteSpace($ArtifactListPath)) {
	$artifactListParent = Split-Path -Parent $ArtifactListPath
	if (-not [string]::IsNullOrWhiteSpace($artifactListParent)) {
		New-Item -ItemType Directory -Force -Path $artifactListParent | Out-Null
	}

	Set-Content -LiteralPath $ArtifactListPath -Value $uniqueArtifacts
	Write-Host "Wrote Windows artifact manifest to '$ArtifactListPath'."
}

Write-Host ""
Write-Host "Collected Windows artifacts:"
foreach ($artifactPath in $uniqueArtifacts) {
	Write-Host $artifactPath
}
