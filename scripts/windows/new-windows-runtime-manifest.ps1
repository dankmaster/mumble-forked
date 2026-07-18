[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$StageRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $StageRoot -PathType Container)) {
	throw "Stage root does not exist: '$StageRoot'."
}

$resolvedRoot = (Resolve-Path -LiteralPath $StageRoot).Path.TrimEnd(
	[IO.Path]::DirectorySeparatorChar,
	[IO.Path]::AltDirectorySeparatorChar
)
if ([string]::IsNullOrWhiteSpace($resolvedRoot) -or
	$resolvedRoot -ceq [IO.Path]::GetPathRoot($resolvedRoot)) {
	throw "Stage root must not be a filesystem root."
}

$manifestPath = Join-Path $resolvedRoot "runtime-manifest.json"
$manifestComparer = [StringComparer]::OrdinalIgnoreCase
$files = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File | Where-Object {
	-not $manifestComparer.Equals($_.FullName, $manifestPath)
})
if ($files.Count -eq 0) {
	throw "Stage root contains no runtime files: '$resolvedRoot'."
}

$filesByRelativePath = @{}
foreach ($file in $files) {
	if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw "Runtime payload must not contain file reparse points: '$($file.FullName)'."
	}
	$relativePath = $file.FullName.Substring($resolvedRoot.Length + 1).Replace('\', '/')
	$parts = @($relativePath -split '/')
	if ([string]::IsNullOrWhiteSpace($relativePath) -or
		[IO.Path]::IsPathRooted($relativePath) -or
		$relativePath.Contains('\') -or
		$parts -contains '' -or
		$parts -contains '.' -or
		$parts -contains '..') {
		throw "Runtime payload contains a non-canonical path: '$relativePath'."
	}
	$key = $relativePath.ToLowerInvariant()
	if ($filesByRelativePath.ContainsKey($key)) {
		throw "Runtime payload contains a duplicate case-insensitive path: '$relativePath'."
	}
	$filesByRelativePath[$key] = [ordered]@{
		path = $relativePath
		size = [int64]$file.Length
		sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
	}
}

$relativePaths = @($filesByRelativePath.Values | ForEach-Object { [string]$_.path } | Sort-Object)
$entries = @($relativePaths | ForEach-Object {
	$filesByRelativePath[$_.ToLowerInvariant()]
})
$document = [ordered]@{
	schema_version = 1
	files = $entries
}
$json = $document | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText($manifestPath, "$json`n", [Text.UTF8Encoding]::new($false))

$written = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([int]$written.schema_version -ne 1 -or @($written.files).Count -ne $entries.Count) {
	throw "Failed to create a complete staged runtime manifest."
}
for ($index = 0; $index -lt $entries.Count; ++$index) {
	$expected = $entries[$index]
	$actual = @($written.files)[$index]
	if ([string]$actual.path -cne [string]$expected.path -or
		[int64]$actual.size -ne [int64]$expected.size -or
		[string]$actual.sha256 -cne [string]$expected.sha256) {
		throw "Runtime manifest readback mismatch at entry $index."
	}
}

Write-Host "Wrote runtime manifest for $($entries.Count) staged file(s): '$manifestPath'."
