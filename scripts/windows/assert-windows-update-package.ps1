[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$PackagePath,

	[string]$ExpectedCommit = "",
	[int]$ExpectedBuild = 0,
	[string]$ExpectedVersion = "",
	[switch]$RequireUpdaterRuntime,
	[switch]$RequireGStreamerRuntime,
	[string]$ExpandedPayloadPath = "",
	[string]$TargetPayloadPath = "",
	[string]$BaseManifestPath = "",
	[string]$ExpectedBaseManifestSha256 = "",
	[string]$DumpbinPath = ""
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

function Get-ManifestRecordMap {
	param([object]$Manifest, [string]$Context)
	$records = [System.Collections.Generic.Dictionary[string,object]]::new(
		[System.StringComparer]::OrdinalIgnoreCase)
	foreach ($record in @($Manifest.files)) {
		Assert-ExactProperties $record @('path', 'size', 'sha256') "$Context file record"
		$relative = Assert-SafePayloadPath ([string]$record.path)
		$sha256 = ([string]$record.sha256).Trim().ToLowerInvariant()
		if ([int64]$record.size -lt 0 -or $sha256 -notmatch '^[0-9a-f]{64}$') {
			throw "$Context contains an invalid file record for '$relative'."
		}
		if ($records.ContainsKey($relative)) {
			throw "$Context contains duplicate path '$relative'."
		}
		$records.Add($relative, [ordered]@{
			path = $relative
			size = [int64]$record.size
			sha256 = $sha256
		})
	}
	if ($records.Count -eq 0) {
		throw "$Context does not list any files."
	}
	return $records
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
		[string]$manifest.packageId -cne 'mumble-forked' -or [int]$manifest.minUpdaterVersion -ne 4 -or
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

	$manifestRecords = Get-ManifestRecordMap -Manifest $manifest -Context 'Update manifest'
	$manifestPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($relative in $manifestRecords.Keys) { [void]$manifestPaths.Add($relative) }

	$baseRecords = [System.Collections.Generic.Dictionary[string,object]]::new(
		[System.StringComparer]::OrdinalIgnoreCase)
	if (-not [string]::IsNullOrWhiteSpace($BaseManifestPath)) {
		if ([string]::IsNullOrWhiteSpace($ExpectedBaseManifestSha256) -or
			$ExpectedBaseManifestSha256.Trim() -notmatch '^[0-9A-Fa-f]{64}$') {
			throw "ExpectedBaseManifestSha256 is required when BaseManifestPath is used."
		}
		$baseItem = Get-Item -LiteralPath $BaseManifestPath -ErrorAction Stop
		$baseHash = Get-ReleaseFileSha256 -Path $baseItem.FullName
		if ($baseHash -cne $ExpectedBaseManifestSha256.Trim().ToLowerInvariant()) {
			throw "Base update file manifest does not match ExpectedBaseManifestSha256."
		}
		$baseManifest = Read-ReleaseJson -Path $baseItem.FullName
		Assert-ExactProperties $baseManifest @(
			'manifestVersion', 'format', 'packageId', 'version', 'build', 'commit',
			'minUpdaterVersion', 'applyMode', 'createdAt', 'healthCheck', 'files'
		) 'Base update manifest'
		if ([int]$baseManifest.manifestVersion -ne 1 -or
			[string]$baseManifest.format -cne 'mumble-update-v1' -or
			[string]$baseManifest.packageId -cne 'mumble-forked') {
			throw "Base update file manifest identity is invalid."
		}
		$baseRecords = Get-ManifestRecordMap -Manifest $baseManifest -Context 'Base update manifest'
	}

	$expectedPayloadPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($relative in $manifestRecords.Keys) {
		$record = $manifestRecords[$relative]
		if (-not $baseRecords.ContainsKey($relative)) {
			[void]$expectedPayloadPaths.Add($relative)
			continue
		}
		$baseRecord = $baseRecords[$relative]
		if ([int64]$baseRecord.size -ne [int64]$record.size -or
			[string]$baseRecord.sha256 -cne [string]$record.sha256) {
			[void]$expectedPayloadPaths.Add($relative)
		}
	}
	$actualPaths = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | ForEach-Object {
		$_.FullName.Substring($payloadRoot.Length).TrimStart('\', '/').Replace('\', '/')
	})
	if ($actualPaths.Count -ne $expectedPayloadPaths.Count -or
		@($actualPaths | Where-Object { -not $expectedPayloadPaths.Contains($_) }).Count) {
		throw "Update payload is not the exact changed-file set for its declared base."
	}
	foreach ($relative in $expectedPayloadPaths) {
		$record = $manifestRecords[$relative]
		$filePath = Join-Path $payloadRoot ($relative.Replace('/', '\'))
		$file = Get-Item -LiteralPath $filePath -ErrorAction Stop
		$hash = Get-ReleaseFileSha256 -Path $file.FullName
		if ($file.PSIsContainer -or [int64]$record.size -ne [int64]$file.Length -or
			[string]$record.sha256 -cne $hash) {
			throw "Update payload file '$relative' does not match its manifest."
		}
	}
	foreach ($required in @('mumble.exe', 'mumble-updater.exe')) {
		if (-not $manifestPaths.Contains($required)) { throw "Update target manifest is missing '$required'." }
	}

	$targetRoot = ""
	if (-not [string]::IsNullOrWhiteSpace($TargetPayloadPath)) {
		$targetRoot = (Resolve-Path -LiteralPath $TargetPayloadPath).Path
		$actualTargetPaths = @(Get-ChildItem -LiteralPath $targetRoot -Recurse -File | ForEach-Object {
			$_.FullName.Substring($targetRoot.Length).TrimStart('\', '/').Replace('\', '/')
		})
		if ($actualTargetPaths.Count -ne $manifestPaths.Count -or
			@($actualTargetPaths | Where-Object { -not $manifestPaths.Contains($_) }).Count) {
			throw "Target payload and update manifest do not contain the exact same file set."
		}
		foreach ($relative in $manifestRecords.Keys) {
			$record = $manifestRecords[$relative]
			$filePath = Join-Path $targetRoot ($relative.Replace('/', '\'))
			$file = Get-Item -LiteralPath $filePath -ErrorAction Stop
			if ($file.PSIsContainer -or [int64]$record.size -ne [int64]$file.Length -or
				[string]$record.sha256 -cne (Get-ReleaseFileSha256 -Path $file.FullName)) {
				throw "Target payload file '$relative' does not match its manifest."
			}
		}
	}

	$runtimeRoot = if ([string]::IsNullOrWhiteSpace($targetRoot)) { $payloadRoot } else { $targetRoot }
	if ($RequireGStreamerRuntime -and
		@(Get-ChildItem -LiteralPath (Join-Path $runtimeRoot 'gstreamer\lib\gstreamer-1.0') -File -Filter '*.dll' -ErrorAction SilentlyContinue).Count -eq 0) {
		throw "Update target is missing the packaged GStreamer plugins."
	}

	if (-not [string]::IsNullOrWhiteSpace($ExpandedPayloadPath)) {
		if ($baseRecords.Count -gt 0 -and [string]::IsNullOrWhiteSpace($targetRoot)) {
			throw "ExpandedPayloadPath for a sparse package requires TargetPayloadPath."
		}
		$destination = [System.IO.Path]::GetFullPath($ExpandedPayloadPath)
		Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction SilentlyContinue
		New-Item -ItemType Directory -Force -Path $destination | Out-Null
		Copy-Item -Path (Join-Path $runtimeRoot '*') -Destination $destination -Recurse -Force
	}
	if ($RequireUpdaterRuntime) {
		$runtimeRoot = if ([string]::IsNullOrWhiteSpace($ExpandedPayloadPath)) { $runtimeRoot } else { $destination }
		& (Join-Path $PSScriptRoot 'assert-mumble-updater-static-runtime.ps1') `
			-UpdaterPath (Join-Path $runtimeRoot 'mumble-updater.exe') -DumpbinPath $DumpbinPath
	}
} finally {
	Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Verified update package '$($package.Name)' ($($actualPaths.Count)/$($manifestPaths.Count) payload files)."
