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

	[int] $MinUpdaterVersion = 2,

	[string] $ApplyMode = "replace-staged-payload",

	[string] $ManifestOutPath = "",

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

	return [System.IO.Path]::GetRelativePath($Root, $Path).Replace('\', '/')
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

		$required = @('mumble.exe', 'mumble-updater.exe')
		foreach ($requiredPath in $required) {
			$requiredFile = Join-Path $payloadRoot $requiredPath
			if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
				throw "Package payload is missing required file: $requiredPath"
			}
		}

		foreach ($entry in $manifest.files) {
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

$stageRootResolved = (Resolve-Path -LiteralPath $StageRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $stageRootResolved 'mumble.exe') -PathType Leaf)) {
	throw "StageRoot is missing mumble.exe: $stageRootResolved"
}

if (-not (Test-Path -LiteralPath (Join-Path $stageRootResolved 'mumble-updater.exe') -PathType Leaf)) {
	throw "StageRoot is missing mumble-updater.exe: $stageRootResolved"
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
