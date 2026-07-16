Param(
	[Parameter(Mandatory = $true)]
	[string] $StageRoot,

	[Parameter(Mandatory = $true)]
	[string] $OutputPath,

	[Parameter(Mandatory = $true)]
	[string] $Version,

	[Parameter(Mandatory = $true)]
	[int] $Build,

	[Parameter(Mandatory = $true)]
	[string] $Commit,

	[string] $PackageId = "mumble-forked",

	[string] $Format = "mumble-update-v1",

	[int] $MinUpdaterVersion = 4,

	[string] $ApplyMode = "replace-staged-payload",

	[string] $ManifestOutPath = "",

	[switch] $RequireUpdaterRuntime,

	[switch] $RequireGStreamerRuntime,

	[switch] $Validate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RelativePackagePath {
	Param(
		[Parameter(Mandatory = $true)]
		[string] $Root,

		[Parameter(Mandatory = $true)]
		[string] $Path
	)

	$rootPath = [System.IO.Path]::GetFullPath($Root)
	$filePath = [System.IO.Path]::GetFullPath($Path)
	if (-not $rootPath.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and -not $rootPath.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
		$rootPath += [System.IO.Path]::DirectorySeparatorChar
	}

	$getRelativePath = [System.IO.Path].GetMethods() | Where-Object { $_.Name -eq "GetRelativePath" } | Select-Object -First 1
	if ($null -ne $getRelativePath) {
		return [System.IO.Path]::GetRelativePath($rootPath, $filePath).Replace('\', '/')
	}

	$rootUri = New-Object System.Uri($rootPath)
	$fileUri = New-Object System.Uri($filePath)
	return [System.Uri]::UnescapeDataString($rootUri.MakeRelativeUri($fileUri).ToString()).Replace('\', '/')
}

function Assert-SafePackagePath {
	Param(
		[Parameter(Mandatory = $true)]
		[string] $RelativePath
	)

	if ([string]::IsNullOrWhiteSpace($RelativePath)) {
		throw "Package manifest contains an empty path"
	}

	if ([System.IO.Path]::IsPathRooted($RelativePath)) {
		throw "Package manifest contains a rooted path: $RelativePath"
	}

	$parts = $RelativePath -split '/'
	if ($parts -contains '..') {
		throw "Package manifest contains a parent traversal path: $RelativePath"
	}
}

function Get-RequiredQtQuickPayloadPaths {
	return @(
		'mumble.exe',
		'mumble-updater.exe',
		'Qt6Core.dll',
		'Qt6Gui.dll',
		'Qt6Qml.dll',
		'Qt6Quick.dll',
		'Qt6QuickControls2.dll',
		'Qt6QuickControls2Basic.dll',
		'Qt6QuickControls2BasicStyleImpl.dll',
		'Qt6QuickControls2Impl.dll',
		'Qt6QuickDialogs2.dll',
		'Qt6QuickDialogs2QuickImpl.dll',
		'Qt6QuickLayouts.dll',
		'Qt6QuickShapes.dll',
		'Qt6QuickTemplates2.dll',
		'Qt6WebEngineCore.dll',
		'Qt6WebEngineQuick.dll',
		'QtWebEngineProcess.exe',
		'platforms/qwindows.dll',
		'tls/qopensslbackend.dll',
		'qml/QtQuick/qmldir',
		'qml/QtQuick/Controls/qmldir',
		'qml/QtQuick/Controls/qtquickcontrols2plugin.dll',
		'qml/QtQuick/Controls/Basic/qmldir',
		'qml/QtQuick/Controls/Basic/qtquickcontrols2basicstyleplugin.dll',
		'qml/QtQuick/Controls/Basic/impl/qmldir',
		'qml/QtQuick/Controls/Basic/impl/qtquickcontrols2basicstyleimplplugin.dll',
		'qml/QtQuick/Controls/impl/qmldir',
		'qml/QtQuick/Controls/impl/qtquickcontrols2implplugin.dll',
		'qml/QtQuick/Layouts/qmldir',
		'qml/QtQuick/Layouts/qquicklayoutsplugin.dll',
		'qml/QtQuick/Dialogs/qmldir',
		'qml/QtQuick/Dialogs/qtquickdialogsplugin.dll',
		'qml/QtQuick/Dialogs/quickimpl/qmldir',
		'qml/QtQuick/Dialogs/quickimpl/qtquickdialogs2quickimplplugin.dll',
		'qml/QtQuick/Shapes/qmldir',
		'qml/QtQuick/Shapes/qmlshapesplugin.dll',
		'qml/QtQuick/Templates/qmldir',
		'qml/QtQuick/Templates/qtquicktemplates2plugin.dll',
		'qml/QtWebEngine/qmldir',
		'qml/QtWebEngine/qtwebenginequickplugin.dll',
		'resources/icudtl.dat',
		'resources/qtwebengine_resources.pak',
		'translations/qtwebengine_locales/en-US.pak',
		'qt.conf',
		'direct-runtime-dependencies.txt'
	)
}

function Assert-QtQuickPayload {
	Param(
		[Parameter(Mandatory = $true)]
		[string] $Root
	)

	$requiredPayloadPaths = @(Get-RequiredQtQuickPayloadPaths)
	foreach ($requiredPath in $requiredPayloadPaths) {
		if (-not (Test-Path -LiteralPath (Join-Path $Root $requiredPath) -PathType Leaf)) {
			throw "Qt Quick payload is missing required file: $requiredPath"
		}
	}

	$runtimeManifestPath = Join-Path $Root 'runtime-manifest.json'
	if (-not (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf)) {
		throw "Qt Quick payload is missing runtime-manifest.json"
	}
	$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
	$runtimeEntries = @($runtimeManifest.files)
	if ($runtimeManifest.schema_version -ne 1 -or $runtimeEntries.Count -eq 0) {
		throw "Qt Quick payload has an invalid or empty runtime manifest"
	}

	$runtimePaths = @($runtimeEntries | ForEach-Object { [string]$_.path })
	foreach ($runtimePath in $runtimePaths) {
		Assert-SafePackagePath -RelativePath $runtimePath
		$runtimePathParts = @($runtimePath -split '/')
		if ($runtimePath.Contains('\') -or $runtimePathParts -contains '.' -or $runtimePathParts -contains '') {
			throw "Runtime manifest contains a non-canonical path: $runtimePath"
		}
	}
	$duplicateRuntimePaths = @($runtimePaths |
		Group-Object { $_.ToLowerInvariant() } |
		Where-Object Count -gt 1 |
		ForEach-Object { $_.Group -join ', ' })
	if ($duplicateRuntimePaths.Count -gt 0) {
		throw "Runtime manifest contains duplicate paths: $($duplicateRuntimePaths -join '; ')"
	}
	$sortedRuntimePaths = @($runtimePaths | Sort-Object)
	for ($index = 0; $index -lt $runtimePaths.Count; ++$index) {
		if ($runtimePaths[$index] -cne $sortedRuntimePaths[$index]) {
			throw "Runtime manifest paths are not in deterministic sorted order"
		}
	}
	foreach ($requiredPath in $requiredPayloadPaths) {
		if ($runtimePaths -notcontains $requiredPath) {
			throw "Runtime manifest is missing required file: $requiredPath"
		}
	}

	$actualRuntimePaths = @(Get-ChildItem -LiteralPath $Root -Recurse -File |
		Where-Object { $_.FullName -ne $runtimeManifestPath } |
		ForEach-Object { Get-RelativePackagePath -Root $Root -Path $_.FullName } |
		Sort-Object)
	$runtimePathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	$actualRuntimePathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($runtimePath in $runtimePaths) {
		[void]$runtimePathSet.Add($runtimePath)
	}
	foreach ($actualRuntimePath in $actualRuntimePaths) {
		[void]$actualRuntimePathSet.Add($actualRuntimePath)
	}
	$unlistedRuntimeFiles = @($actualRuntimePaths | Where-Object { -not $runtimePathSet.Contains($_) })
	$missingRuntimeFiles = @($runtimePaths | Where-Object { -not $actualRuntimePathSet.Contains($_) })
	if ($unlistedRuntimeFiles.Count -gt 0 -or $missingRuntimeFiles.Count -gt 0) {
		throw "Runtime manifest does not exactly cover the payload (unlisted: $($unlistedRuntimeFiles -join ', '); missing: $($missingRuntimeFiles -join ', '))"
	}

	foreach ($entry in $runtimeEntries) {
		$filePath = Join-Path $Root ([string]$entry.path)
		$item = Get-Item -LiteralPath $filePath
		if ([int64]$entry.size -ne [int64]$item.Length) {
			throw "Runtime manifest size mismatch for $($entry.path)"
		}
		$hash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($hash -ne ([string]$entry.sha256).ToLowerInvariant()) {
			throw "Runtime manifest SHA256 mismatch for $($entry.path)"
		}
	}

	$directDependencies = @(Get-Content -LiteralPath (Join-Path $Root 'direct-runtime-dependencies.txt') |
		ForEach-Object { ([string]$_).Trim() } |
		Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	$invalidDirectDependencies = @($directDependencies | Where-Object { $_ -notmatch '^[A-Za-z0-9_.+-]+\.(?:dll|drv|cpl)$' })
	if ($invalidDirectDependencies.Count -gt 0) {
		throw "Direct runtime dependency report contains invalid entries: $($invalidDirectDependencies -join ', ')"
	}
	foreach ($requiredDirectRuntime in @('Qt6Quick.dll', 'Qt6Qml.dll', 'Qt6WebEngineQuick.dll', 'Qt6WebEngineCore.dll')) {
		if ($directDependencies -notcontains $requiredDirectRuntime) {
			throw "Direct runtime dependency report is missing: $requiredDirectRuntime"
		}
	}
	$forbiddenDirectRuntimes = @(
		'Qt6QuickWidgets.dll',
		'Qt6WebEngineWidgets.dll',
		'Qt6WebChannel.dll',
		'Qt6WebChannelQuick.dll'
	)
	$forbiddenDirectImports = @($forbiddenDirectRuntimes | Where-Object { $directDependencies -contains $_ })
	if ($forbiddenDirectImports.Count -gt 0) {
		throw "Payload directly imports compatibility or app-bridge runtimes: $($forbiddenDirectImports -join ', ')"
	}
	$forbiddenPayloadRuntimes = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
		$_.Name -in @('Qt6QuickWidgets.dll', 'Qt6WebEngineWidgets.dll')
	})
	if ($forbiddenPayloadRuntimes.Count -gt 0) {
		throw "Payload contains forbidden compatibility runtimes: $($forbiddenPayloadRuntimes.FullName -join ', ')"
	}
}

function Test-PackageArchive {
	Param(
		[Parameter(Mandatory = $true)]
		[string] $PackagePath
	)

	$validationRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mumble-update-validate-" + [System.Guid]::NewGuid().ToString("N"))
	$validationZip = Join-Path $validationRoot "package.zip"
	New-Item -ItemType Directory -Force -Path $validationRoot | Out-Null

	try {
		Copy-Item -LiteralPath $PackagePath -Destination $validationZip -Force
		Expand-Archive -LiteralPath $validationZip -DestinationPath $validationRoot -Force

		$manifestPath = Join-Path $validationRoot "manifest.json"
		if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
			throw "Package archive does not contain manifest.json"
		}

		$payloadRoot = Join-Path $validationRoot "payload"
		if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container)) {
			throw "Package archive does not contain payload/"
		}

		$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
		if ($manifest.format -ne $Format) {
			throw "Unexpected package format '$($manifest.format)'"
		}
		if ($null -eq $manifest.healthCheck -or $manifest.healthCheck.required -ne $true -or
			[int64] $manifest.healthCheck.minimumStableRuntimeMilliseconds -ne 10000 -or
			[int64] $manifest.healthCheck.timeoutMilliseconds -ne 45000) {
			throw "Package archive does not require the production health-marker contract"
		}
		Assert-QtQuickPayload -Root $payloadRoot

		$required = @(Get-RequiredQtQuickPayloadPaths) + @('runtime-manifest.json')
		if ($RequireGStreamerRuntime) {
			$required += @(
				'gstreamer/bin/gst-launch-1.0.exe',
				'gstreamer/bin/gst-inspect-1.0.exe',
				'gstreamer/libexec/gstreamer-1.0/gst-plugin-scanner.exe'
			)
		}
		foreach ($requiredPath in $required) {
			$requiredFile = Join-Path $payloadRoot $requiredPath
			if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
				throw "Package payload is missing required file: $requiredPath"
			}
		}

		if ($RequireGStreamerRuntime) {
			$pluginRoot = Join-Path $payloadRoot 'gstreamer/lib/gstreamer-1.0'
			if (-not (Test-Path -LiteralPath $pluginRoot -PathType Container)) {
				throw "Package payload is missing required GStreamer plugin directory: gstreamer/lib/gstreamer-1.0"
			}

			$pluginFiles = @(Get-ChildItem -Path $pluginRoot -File -Filter '*.dll' -ErrorAction SilentlyContinue)
			if ($pluginFiles.Count -eq 0) {
				throw "Package payload is missing GStreamer plugin DLLs under gstreamer/lib/gstreamer-1.0"
			}
		}

		$packageEntries = @($manifest.files)
		$packagePaths = @($packageEntries | ForEach-Object { [string]$_.path })
		$duplicatePackagePaths = @($packagePaths |
			Group-Object { $_.ToLowerInvariant() } |
			Where-Object Count -gt 1 |
			ForEach-Object { $_.Group -join ', ' })
		if ($duplicatePackagePaths.Count -gt 0) {
			throw "Package manifest contains duplicate paths: $($duplicatePackagePaths -join '; ')"
		}
		$sortedPackagePaths = @($packagePaths | Sort-Object)
		for ($index = 0; $index -lt $packagePaths.Count; ++$index) {
			if ($packagePaths[$index] -cne $sortedPackagePaths[$index]) {
				throw "Package manifest paths are not in deterministic sorted order"
			}
		}
		$actualPackagePaths = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File |
			ForEach-Object { Get-RelativePackagePath -Root $payloadRoot -Path $_.FullName } |
			Sort-Object)
		$packagePathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
		$actualPackagePathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
		foreach ($packagePath in $packagePaths) {
			[void]$packagePathSet.Add($packagePath)
		}
		foreach ($actualPackagePath in $actualPackagePaths) {
			[void]$actualPackagePathSet.Add($actualPackagePath)
		}
		$unlistedPackageFiles = @($actualPackagePaths | Where-Object { -not $packagePathSet.Contains($_) })
		$missingPackageFiles = @($packagePaths | Where-Object { -not $actualPackagePathSet.Contains($_) })
		if ($unlistedPackageFiles.Count -gt 0 -or $missingPackageFiles.Count -gt 0) {
			throw "Package manifest does not exactly cover payload/ (unlisted: $($unlistedPackageFiles -join ', '); missing: $($missingPackageFiles -join ', '))"
		}

		foreach ($entry in $packageEntries) {
			Assert-SafePackagePath -RelativePath $entry.path
			$filePath = Join-Path $payloadRoot ($entry.path -replace '/', [System.IO.Path]::DirectorySeparatorChar)
			if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
				throw "Package manifest references a missing file: $($entry.path)"
			}

			$item = Get-Item -LiteralPath $filePath
			if ([int64] $entry.size -ne [int64] $item.Length) {
				throw "Size mismatch for $($entry.path)"
			}

			$hash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
			if ($hash -ne ([string] $entry.sha256).ToLowerInvariant()) {
				throw "SHA256 mismatch for $($entry.path)"
			}
		}
	} finally {
		if (Test-Path -LiteralPath $validationRoot) {
			Remove-Item -LiteralPath $validationRoot -Recurse -Force
		}
	}
}

function Assert-GStreamerPayload {
	Param(
		[Parameter(Mandatory = $true)]
		[string] $Root
	)

	$required = @(
		'gstreamer\bin\gst-launch-1.0.exe',
		'gstreamer\bin\gst-inspect-1.0.exe',
		'gstreamer\lib\gstreamer-1.0',
		'gstreamer\libexec\gstreamer-1.0\gst-plugin-scanner.exe'
	)

	$missing = New-Object System.Collections.Generic.List[string]
	foreach ($relativePath in $required) {
		$fullPath = Join-Path $Root $relativePath
		if (-not (Test-Path -LiteralPath $fullPath)) {
			$missing.Add($relativePath)
		}
	}

	$pluginFiles = @(Get-ChildItem -Path (Join-Path $Root 'gstreamer\lib\gstreamer-1.0') -File -Filter '*.dll' -ErrorAction SilentlyContinue)
	if ($pluginFiles.Count -eq 0) {
		$missing.Add('gstreamer\lib\gstreamer-1.0\*.dll')
	}

	if ($missing.Count -gt 0) {
		throw "StageRoot is missing required GStreamer update payload files: $($missing -join ', ')."
	}
}

$stageRootResolved = (Resolve-Path -LiteralPath $StageRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $stageRootResolved 'mumble.exe') -PathType Leaf)) {
	throw "StageRoot is missing mumble.exe: $stageRootResolved"
}

if (-not (Test-Path -LiteralPath (Join-Path $stageRootResolved 'mumble-updater.exe') -PathType Leaf)) {
	throw "StageRoot is missing mumble-updater.exe: $stageRootResolved"
}
Assert-QtQuickPayload -Root $stageRootResolved

if ($RequireGStreamerRuntime) {
	Assert-GStreamerPayload -Root $stageRootResolved
}

$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFullPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$packageRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mumble-update-package-" + [System.Guid]::NewGuid().ToString("N"))
$payloadRoot = Join-Path $packageRoot "payload"

try {
	New-Item -ItemType Directory -Force -Path $payloadRoot | Out-Null
	Copy-Item -Path (Join-Path $stageRootResolved '*') -Destination $payloadRoot -Recurse -Force

	$files = Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
		$relativePath = Get-RelativePackagePath -Root $payloadRoot -Path $_.FullName
		Assert-SafePackagePath -RelativePath $relativePath
		[ordered] @{
			path = $relativePath
			size = [int64] $_.Length
			sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
		}
	}

	$manifest = [ordered] @{
		manifestVersion = 1
		format = $Format
		packageId = $PackageId
		version = $Version
		build = $Build
		commit = $Commit
		minUpdaterVersion = $MinUpdaterVersion
		applyMode = $ApplyMode
		createdAt = (Get-Date).ToUniversalTime().ToString("o")
		healthCheck = [ordered] @{
			required = $true
			minimumStableRuntimeMilliseconds = 10000
			timeoutMilliseconds = 45000
		}
		files = @($files)
	}

	$manifestPath = Join-Path $packageRoot "manifest.json"
	$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8

	if (Test-Path -LiteralPath $outputFullPath) {
		Remove-Item -LiteralPath $outputFullPath -Force
	}

	$tempZipPath = Join-Path ([System.IO.Path]::GetTempPath()) ("mumble-update-" + [System.Guid]::NewGuid().ToString("N") + ".zip")
	try {
		$archivePaths = @((Join-Path $packageRoot 'manifest.json'), (Join-Path $packageRoot 'payload'))
		Compress-Archive -LiteralPath $archivePaths -DestinationPath $tempZipPath -CompressionLevel Optimal -Force
		Move-Item -LiteralPath $tempZipPath -Destination $outputFullPath -Force
	} finally {
		if (Test-Path -LiteralPath $tempZipPath) {
			Remove-Item -LiteralPath $tempZipPath -Force
		}
	}

	if ($Validate) {
		Test-PackageArchive -PackagePath $outputFullPath
	}

	$packageItem = Get-Item -LiteralPath $outputFullPath
	$packageHash = (Get-FileHash -LiteralPath $outputFullPath -Algorithm SHA256).Hash.ToLowerInvariant()

	if (-not [string]::IsNullOrWhiteSpace($ManifestOutPath)) {
		$metadataFullPath = [System.IO.Path]::GetFullPath($ManifestOutPath)
		$metadataDirectory = Split-Path -Parent $metadataFullPath
		if (-not [string]::IsNullOrWhiteSpace($metadataDirectory)) {
			New-Item -ItemType Directory -Force -Path $metadataDirectory | Out-Null
		}

		[ordered] @{
			format = $Format
			sha256 = $packageHash
			size = [int64] $packageItem.Length
			minUpdaterVersion = $MinUpdaterVersion
			applyMode = $ApplyMode
		} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataFullPath -Encoding utf8
	}

	Write-Host "Created $outputFullPath"
	Write-Host "SHA256 $packageHash"
	Write-Host "Size $($packageItem.Length)"
} finally {
	if (Test-Path -LiteralPath $packageRoot) {
		Remove-Item -LiteralPath $packageRoot -Recurse -Force
	}
}
