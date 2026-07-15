[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$PackagePath,

	[string]$ExpectedCommit = "",
	[int]$ExpectedBuild = 0,
	[string]$ExpectedVersion = "",
	[switch]$RequireUpdaterRuntime,
	[switch]$RequireGStreamerRuntime,
	[string]$ExpandedPayloadPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

function Assert-ExactProperties {
	param([object]$Object, [string[]]$Names, [string]$Context)
	$actual = @($Object.PSObject.Properties.Name)
	$missing = @($Names | Where-Object { $_ -cnotin $actual })
	$extra = @($actual | Where-Object { $_ -cnotin $Names })
	if ($missing.Count -or $extra.Count) {
		throw "$Context schema mismatch. Missing: [$($missing -join ', ')]; unexpected: [$($extra -join ', ')]."
	}
}

function Assert-SafePayloadPath {
	param([string]$Path)
	if ([string]::IsNullOrWhiteSpace($Path) -or $Path.Contains('\') -or
		[System.IO.Path]::IsPathRooted($Path) -or $Path.StartsWith('/') -or
		@($Path.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0) {
		throw "Unsafe update-package path '$Path'."
	}
	return $Path
}

$package = Get-Item -LiteralPath $PackagePath -ErrorAction Stop
if ($package.PSIsContainer -or $package.Extension -cne '.mumble-update') {
	throw "Update package must be a .mumble-update file."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($package.FullName)
$entryNames = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
try {
	foreach ($entry in $archive.Entries) {
		$name = $entry.FullName.Replace('\', '/')
		if ([string]::IsNullOrWhiteSpace($name) -or $name.StartsWith('/') -or
			$name.Contains(':') -or @($name.Split('/') | Where-Object { $_ -in @('.', '..') }).Count -ne 0 -or
			-not $entryNames.Add($name)) {
			throw "Update archive contains an unsafe or duplicate entry '$name'."
		}
		if (-not ($name -ceq 'manifest.json' -or $name.StartsWith('payload/', [System.StringComparison]::Ordinal))) {
			throw "Update archive contains unexpected root entry '$name'."
		}
	}
} finally {
	$archive.Dispose()
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("mumble-update-assert-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
try {
	$zipPath = Join-Path $temporaryRoot 'package.zip'
	Copy-Item -LiteralPath $package.FullName -Destination $zipPath
	$expandedRoot = Join-Path $temporaryRoot 'expanded'
	Expand-Archive -LiteralPath $zipPath -DestinationPath $expandedRoot
	$manifestPath = Join-Path $expandedRoot 'manifest.json'
	$payloadRoot = Join-Path $expandedRoot 'payload'
	if (-not (Test-Path $manifestPath -PathType Leaf) -or -not (Test-Path $payloadRoot -PathType Container)) {
		throw "Update package must contain manifest.json and payload/."
	}

	$manifest = Read-ReleaseJson -Path $manifestPath
	Assert-ExactProperties $manifest @(
		'manifestVersion', 'format', 'packageId', 'version', 'build', 'commit',
		'minUpdaterVersion', 'applyMode', 'createdAt', 'healthCheck', 'files'
	) 'Update manifest'
	if ([int]$manifest.manifestVersion -ne 1 -or [string]$manifest.format -cne 'mumble-update-v1' -or
		[string]$manifest.packageId -cne 'mumble-forked' -or [int]$manifest.minUpdaterVersion -ne 3 -or
		[string]$manifest.applyMode -cne 'replace-staged-payload') {
		throw "Update manifest identity or updater contract is invalid."
	}
	$health = Assert-ObjectProperty -Object $manifest -Name 'healthCheck' -Context 'Update manifest'
	Assert-ExactProperties $health @('required', 'minimumStableRuntimeMilliseconds', 'timeoutMilliseconds') 'Update health contract'
	if ($health.required -ne $true -or [int64]$health.minimumStableRuntimeMilliseconds -ne 10000 -or
		[int64]$health.timeoutMilliseconds -ne 45000) {
		throw "Update package does not require the production health-marker contract."
	}
	$created = [datetimeoffset]::MinValue
	if (-not [datetimeoffset]::TryParse([string]$manifest.createdAt,
		[Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$created)) {
		throw "Update manifest createdAt is invalid."
	}
	if (-not [string]::IsNullOrWhiteSpace($ExpectedCommit) -and
		(Assert-FullGitSha -Sha ([string]$manifest.commit) -Context 'Update commit') -cne
		(Assert-FullGitSha -Sha $ExpectedCommit -Context 'Expected update commit')) {
		throw "Update package belongs to another source commit."
	}
	if ($ExpectedBuild -gt 0 -and [int]$manifest.build -ne $ExpectedBuild) {
		throw "Update package belongs to build '$($manifest.build)', expected '$ExpectedBuild'."
	}
	if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and [string]$manifest.version -cne $ExpectedVersion) {
		throw "Update package version '$($manifest.version)' does not match '$ExpectedVersion'."
	}

	$manifestPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($record in @($manifest.files)) {
		Assert-ExactProperties $record @('path', 'size', 'sha256') 'Update file record'
		$relative = Assert-SafePayloadPath ([string]$record.path)
		if (-not $manifestPaths.Add($relative)) { throw "Duplicate update manifest path '$relative'." }
		$filePath = Join-Path $payloadRoot ($relative.Replace('/', '\'))
		$file = Get-Item -LiteralPath $filePath -ErrorAction Stop
		$hash = Get-ReleaseFileSha256 -Path $file.FullName
		if ($file.PSIsContainer -or [int64]$record.size -ne [int64]$file.Length -or
			[string]$record.sha256 -cne $hash) {
			throw "Update payload file '$relative' does not match its manifest."
		}
	}
	$actualPaths = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | ForEach-Object {
		$_.FullName.Substring($payloadRoot.Length).TrimStart('\', '/').Replace('\', '/')
	})
	if ($actualPaths.Count -ne $manifestPaths.Count -or @($actualPaths | Where-Object { -not $manifestPaths.Contains($_) }).Count) {
		throw "Update payload and manifest do not contain the exact same file set."
	}
	foreach ($required in @('mumble.exe', 'mumble-updater.exe')) {
		if (-not $manifestPaths.Contains($required)) { throw "Update payload is missing '$required'." }
	}
	if ($RequireUpdaterRuntime -and -not $manifestPaths.Contains('zlib1.dll')) {
		throw "Update payload is missing zlib1.dll."
	}
	if ($RequireGStreamerRuntime -and @($actualPaths | Where-Object { $_ -like 'gstreamer/lib/gstreamer-1.0/*.dll' }).Count -eq 0) {
		throw "Update payload is missing the packaged GStreamer plugins."
	}

	if (-not [string]::IsNullOrWhiteSpace($ExpandedPayloadPath)) {
		$destination = [System.IO.Path]::GetFullPath($ExpandedPayloadPath)
		Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction SilentlyContinue
		New-Item -ItemType Directory -Force -Path $destination | Out-Null
		Copy-Item -Path (Join-Path $payloadRoot '*') -Destination $destination -Recurse -Force
	}
} finally {
	Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Verified update package '$($package.Name)' ($($manifestPaths.Count) payload files)."
