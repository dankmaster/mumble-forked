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

function Assert-SpeechCleanupPayload {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$requiredRelativePaths = @(
		"onnxruntime.dll",
		"deepfilter.dll",
		"rnnoise.dll",
		"dtln\baseline\model_1.onnx",
		"dtln\baseline\model_2.onnx",
		"dtln\norm_500h\model_1.onnx",
		"dtln\norm_500h\model_2.onnx",
		"dtln\norm_40h\model_1.onnx",
		"dtln\norm_40h\model_2.onnx",
		"rnnoise\rnnoise_little.weights_blob.bin"
	)

	$missing = New-Object System.Collections.Generic.List[string]
	foreach ($relativePath in $requiredRelativePaths) {
		$fullPath = Join-Path $Root $relativePath
		if (-not (Test-Path -LiteralPath $fullPath)) {
			$missing.Add($relativePath)
		}
	}

	$deepFilterArchive = Get-ChildItem -Path (Join-Path $Root "deepfilternet") -File -Filter "*.tar.gz" -ErrorAction SilentlyContinue |
		Select-Object -First 1
	if (-not $deepFilterArchive) {
		$missing.Add("deepfilternet\*.tar.gz")
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required speech-cleanup payload files: $($missing -join ', ')."
	}

	Write-Host "$Label speech-cleanup payload verified."
}

function Assert-UpdaterRuntimePayload {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$requiredRelativePaths = @(
		"zlib1.dll"
	)

	$missing = New-Object System.Collections.Generic.List[string]
	foreach ($relativePath in $requiredRelativePaths) {
		$fullPath = Join-Path $Root $relativePath
		if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
			$missing.Add($relativePath)
		}
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required updater runtime files: $($missing -join ', ')."
	}

	Write-Host "$Label updater runtime payload verified."
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
		"Qt6Qml.dll",
		"Qt6Quick.dll",
		"Qt6WebEngineCore.dll",
		"Qt6WebEngineQuick.dll",
		"QtWebEngineProcess.exe",
		"platforms/qwindows.dll",
		"tls/qopensslbackend.dll",
		"qml/QtQuick/qmldir",
		"qml/QtQuick/Controls/qmldir",
		"qml/QtQuick/Layouts/qmldir",
		"qml/QtQuick/Dialogs/qmldir",
		"qml/QtWebEngine/qmldir",
		"qml/QtWebEngine/qtwebenginequickplugin.dll",
		"resources/icudtl.dat",
		"resources/qtwebengine_resources.pak",
		"translations/qtwebengine_locales/en-US.pak",
		"qt.conf",
		"direct-runtime-dependencies.txt"
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
	foreach ($requiredDirectRuntime in @("Qt6Quick.dll", "Qt6Qml.dll", "Qt6WebEngineQuick.dll", "Qt6WebEngineCore.dll")) {
		if ($directDependencies -notcontains $requiredDirectRuntime) {
			throw "$Label direct runtime dependency report is missing '$requiredDirectRuntime'."
		}
	}
	$forbiddenDirectRuntimes = @(
		"Qt6QuickWidgets.dll",
		"Qt6WebEngineWidgets.dll",
		"Qt6WebChannel.dll",
		"Qt6WebChannelQuick.dll"
	)
	$forbiddenDirectImports = @($forbiddenDirectRuntimes | Where-Object { $directDependencies -contains $_ })
	if ($forbiddenDirectImports.Count -gt 0) {
		throw "$Label directly imports compatibility or app-bridge runtimes: $($forbiddenDirectImports -join ', ')."
	}
	$forbiddenPayloadRuntimes = @($manifestPaths | Where-Object {
		[System.IO.Path]::GetFileName($_) -in @("Qt6QuickWidgets.dll", "Qt6WebEngineWidgets.dll")
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

foreach ($verifiedPath in Assert-BinarySet -Label "Build root '$buildRootPath'" -Root $buildRootPath -BinaryNames $requiredBinaryNames) {
	$allArtifacts.Add($verifiedPath)
}

if ($RequireSpeechCleanup) {
	Assert-SpeechCleanupPayload -Label "Build root '$buildRootPath'" -Root $buildRootPath
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
	Assert-QmlRuntimeManifest -Label "Stage root '$stageRootPath'" -Root $stageRootPath
	foreach ($verifiedPath in Assert-BinarySet -Label "Stage root '$stageRootPath'" -Root $stageRootPath -BinaryNames $requiredBinaryNames) {
		$allArtifacts.Add($verifiedPath)
	}
	if ($RequireSpeechCleanup) {
		Assert-SpeechCleanupPayload -Label "Stage root '$stageRootPath'" -Root $stageRootPath
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
