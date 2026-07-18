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
	[switch]$RequireGStreamerRuntime,
	[string]$CandidateExecutablePath = "",
	[string]$MsiPayloadEvidencePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$msiPayloadVerificationRequested = -not [string]::IsNullOrWhiteSpace($CandidateExecutablePath) -or
	-not [string]::IsNullOrWhiteSpace($MsiPayloadEvidencePath)
if ($msiPayloadVerificationRequested -and
	([string]::IsNullOrWhiteSpace($CandidateExecutablePath) -or [string]::IsNullOrWhiteSpace($MsiPayloadEvidencePath))) {
	throw "CandidateExecutablePath and MsiPayloadEvidencePath must be supplied together."
}
if ($msiPayloadVerificationRequested -and -not $RequireClientInstaller) {
	throw "Candidate MSI payload verification requires RequireClientInstaller."
}

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

function Assert-QmlRuntimeManifest {
	param(
		[Parameter(Mandatory = $true)][string]$Label,
		[Parameter(Mandatory = $true)][string]$Root
	)
	$manifestPath = Join-Path $Root "runtime-manifest.json"
	if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
		throw "$Label is missing runtime-manifest.json."
	}
	$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
	if ($manifest.schema_version -ne 1 -or @($manifest.files).Count -eq 0) {
		throw "$Label has an invalid or empty runtime manifest."
	}

	$manifestEntries = @($manifest.files)
	$manifestPaths = @($manifestEntries | ForEach-Object { [string]$_.path })
	foreach ($manifestRelativePath in $manifestPaths) {
		$pathParts = @($manifestRelativePath -split '/')
		if ([string]::IsNullOrWhiteSpace($manifestRelativePath) `
			-or [System.IO.Path]::IsPathRooted($manifestRelativePath) `
			-or $manifestRelativePath.Contains('\') `
			-or $pathParts -contains '' `
			-or $pathParts -contains '.' `
			-or $pathParts -contains '..') {
			throw "$Label runtime manifest contains an unsafe path: '$manifestRelativePath'."
		}
	}

	$duplicateManifestPaths = @($manifestPaths |
		Group-Object { $_.ToLowerInvariant() } |
		Where-Object Count -gt 1 |
		ForEach-Object { $_.Group -join ', ' })
	if ($duplicateManifestPaths.Count -gt 0) {
		throw "$Label runtime manifest contains duplicate paths: $($duplicateManifestPaths -join '; ')."
	}

	$sortedManifestPaths = @($manifestPaths | Sort-Object)
	for ($index = 0; $index -lt $manifestPaths.Count; ++$index) {
		if ($manifestPaths[$index] -cne $sortedManifestPaths[$index]) {
			throw "$Label runtime manifest paths are not in deterministic sorted order."
		}
	}

	$requiredRuntimePaths = @(
		"mumble.exe",
		"mumble-updater.exe",
		"Qt6Core.dll",
		"Qt6Gui.dll",
		"Qt6Multimedia.dll",
		"Qt6MultimediaQuick.dll",
		"Qt6Qml.dll",
		"Qt6Quick.dll",
		"Qt6QuickControls2.dll",
		"Qt6QuickControls2Basic.dll",
		"Qt6QuickControls2BasicStyleImpl.dll",
		"Qt6QuickControls2Impl.dll",
		"Qt6QuickDialogs2.dll",
		"Qt6QuickDialogs2QuickImpl.dll",
		"Qt6QuickLayouts.dll",
		"Qt6QuickShapes.dll",
		"Qt6QuickTemplates2.dll",
		"Qt6WebEngineCore.dll",
		"Qt6WebEngineQuick.dll",
		"QtWebEngineProcess.exe",
		"platforms/qwindows.dll",
		"multimedia/windowsmediaplugin.dll",
		"tls/qopensslbackend.dll",
		"qml/QtMultimedia/qmldir",
		"qml/QtMultimedia/plugins.qmltypes",
		"qml/QtMultimedia/quickmultimediaplugin.dll",
		"qml/QtMultimedia/Video.qml",
		"qml/QtQuick/qmldir",
		"qml/QtQuick/Controls/qmldir",
		"qml/QtQuick/Controls/qtquickcontrols2plugin.dll",
		"qml/QtQuick/Controls/Basic/qmldir",
		"qml/QtQuick/Controls/Basic/qtquickcontrols2basicstyleplugin.dll",
		"qml/QtQuick/Controls/Basic/impl/qmldir",
		"qml/QtQuick/Controls/Basic/impl/qtquickcontrols2basicstyleimplplugin.dll",
		"qml/QtQuick/Controls/impl/qmldir",
		"qml/QtQuick/Controls/impl/qtquickcontrols2implplugin.dll",
		"qml/QtQuick/Layouts/qmldir",
		"qml/QtQuick/Layouts/qquicklayoutsplugin.dll",
		"qml/QtQuick/Dialogs/qmldir",
		"qml/QtQuick/Dialogs/qtquickdialogsplugin.dll",
		"qml/QtQuick/Dialogs/quickimpl/qmldir",
		"qml/QtQuick/Dialogs/quickimpl/qtquickdialogs2quickimplplugin.dll",
		"qml/QtQuick/Shapes/qmldir",
		"qml/QtQuick/Shapes/qmlshapesplugin.dll",
		"qml/QtQuick/Templates/qmldir",
		"qml/QtQuick/Templates/qtquicktemplates2plugin.dll",
		"qml/QtWebEngine/qmldir",
		"qml/QtWebEngine/qtwebenginequickplugin.dll",
		"resources/icudtl.dat",
		"resources/qtwebengine_resources.pak",
		"translations/qtwebengine_locales/en-US.pak",
		"qt.conf",
		"direct-runtime-dependencies.txt",
		"delay-load-runtime-dependencies.txt"
	)
	foreach ($requiredRuntimePath in $requiredRuntimePaths) {
		if ($manifestPaths -notcontains $requiredRuntimePath) {
			throw "$Label runtime manifest is missing '$requiredRuntimePath'."
		}
	}

	$actualRuntimePaths = @(Get-ChildItem -LiteralPath $Root -Recurse -File |
		Where-Object { $_.FullName -ne $manifestPath } |
		ForEach-Object { $_.FullName.Substring($Root.TrimEnd('\').Length + 1).Replace('\', '/') } |
		Sort-Object)
	$manifestPathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	$actualPathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($path in $manifestPaths) {
		[void]$manifestPathSet.Add($path)
	}
	foreach ($path in $actualRuntimePaths) {
		[void]$actualPathSet.Add($path)
	}
	$missingFromManifest = @($actualRuntimePaths | Where-Object { -not $manifestPathSet.Contains($_) })
	$missingFromPayload = @($manifestPaths | Where-Object { -not $actualPathSet.Contains($_) })
	if ($missingFromManifest.Count -gt 0 -or $missingFromPayload.Count -gt 0) {
		throw "$Label runtime manifest does not exactly cover the staged payload (unlisted: $($missingFromManifest -join ', '); missing: $($missingFromPayload -join ', '))."
	}

	$directDependencyPath = Join-Path $Root "direct-runtime-dependencies.txt"
	$directDependencies = @(Get-Content -LiteralPath $directDependencyPath |
		ForEach-Object { ([string]$_).Trim() } |
		Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	$invalidDirectDependencies = @($directDependencies | Where-Object { $_ -notmatch '^[A-Za-z0-9_.+-]+\.(?:dll|drv|cpl)$' })
	if ($invalidDirectDependencies.Count -gt 0) {
		throw "$Label direct runtime dependency report contains invalid entries: $($invalidDirectDependencies -join ', ')."
	}
	foreach ($requiredDirectRuntime in @("Qt6Quick.dll", "Qt6Qml.dll")) {
		if ($directDependencies -notcontains $requiredDirectRuntime) {
			throw "$Label direct runtime dependency report is missing '$requiredDirectRuntime'."
		}
	}
	$delayLoadDependencies = @(Get-Content -LiteralPath (Join-Path $Root "delay-load-runtime-dependencies.txt") |
		ForEach-Object { ([string]$_).Trim() } |
		Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	$invalidDelayLoadDependencies = @($delayLoadDependencies | Where-Object { $_ -notmatch '^[A-Za-z0-9_.+-]+\.(?:dll|drv|cpl)$' })
	if ($invalidDelayLoadDependencies.Count -gt 0) {
		throw "$Label delay-load runtime dependency report contains invalid entries: $($invalidDelayLoadDependencies -join ', ')."
	}
	$requiredDelayLoadRuntimes = [Collections.Generic.List[string]]::new()
	$requiredDelayLoadRuntimes.Add("Qt6WebEngineQuick.dll")
	$requiredDelayLoadRuntimes.Add("Qt6WebEngineCore.dll")
	foreach ($optionalNeuralRuntime in @("rnnoise.dll", "onnxruntime.dll")) {
		if (Test-Path -LiteralPath (Join-Path $Root $optionalNeuralRuntime) -PathType Leaf) {
			$requiredDelayLoadRuntimes.Add($optionalNeuralRuntime)
		}
	}
	foreach ($requiredDelayLoadRuntime in $requiredDelayLoadRuntimes) {
		if ($delayLoadDependencies -notcontains $requiredDelayLoadRuntime) {
			throw "$Label delay-load runtime dependency report is missing '$requiredDelayLoadRuntime'."
		}
	}
	$forbiddenDirectRuntimes = @(
		"Qt6MultimediaWidgets.dll",
		"Qt6QuickWidgets.dll",
		"Qt6WebEngineWidgets.dll",
		"Qt6WebChannel.dll",
		"Qt6WebChannelQuick.dll",
		"Qt6WebEngineQuick.dll",
		"Qt6WebEngineCore.dll",
		"rnnoise.dll",
		"onnxruntime.dll"
	)
	$forbiddenDirectImports = @($forbiddenDirectRuntimes | Where-Object { $directDependencies -contains $_ })
	if ($forbiddenDirectImports.Count -gt 0) {
		throw "$Label directly imports compatibility, app-bridge, or media-only runtimes: $($forbiddenDirectImports -join ', ')."
	}
	$forbiddenPayloadRuntimes = @($manifestPaths | Where-Object {
		[System.IO.Path]::GetFileName($_) -in @("Qt6MultimediaWidgets.dll", "Qt6QuickWidgets.dll", "Qt6WebEngineWidgets.dll")
	})
	if ($forbiddenPayloadRuntimes.Count -gt 0) {
		throw "$Label contains forbidden compatibility runtimes: $($forbiddenPayloadRuntimes -join ', ')."
	}

	foreach ($entry in $manifestEntries) {
		$filePath = Join-Path $Root ([string]$entry.path).Replace('/', '\')
		if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
			throw "$Label runtime manifest references a missing file: '$($entry.path)'."
		}
		$actualSize = (Get-Item -LiteralPath $filePath).Length
		if ([int64]$entry.size -ne [int64]$actualSize) {
			throw "$Label runtime manifest size mismatch for '$($entry.path)'."
		}
		$actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($actualHash -ne ([string]$entry.sha256).ToLowerInvariant()) {
			throw "$Label runtime manifest hash mismatch for '$($entry.path)'."
		}
	}
	Write-Host "$Label QML runtime manifest verified."
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
	$clientMsiPaths = @(Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*client*.msi" -Description "client installer MSI")
	foreach ($artifactPath in $clientMsiPaths) {
		$allArtifacts.Add($artifactPath)
	}
	if ($msiPayloadVerificationRequested) {
		if ($clientMsiPaths.Count -ne 1) {
			throw "Candidate MSI payload verification requires exactly one client MSI; found $($clientMsiPaths.Count)."
		}
		& "$PSScriptRoot\verify-windows-msi-payload.ps1" `
			-CandidateClientMsi $clientMsiPaths[0] `
			-CandidateExecutable $CandidateExecutablePath `
			-OutputPath $MsiPayloadEvidencePath | Out-Host
		$allArtifacts.Add((Resolve-Path -LiteralPath $MsiPayloadEvidencePath).Path)
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
	Assert-QmlRuntimeManifest -Label "Stage root '$stageRootPath'" -Root $stageRootPath
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
