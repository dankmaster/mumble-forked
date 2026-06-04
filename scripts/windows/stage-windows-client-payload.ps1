[CmdletBinding()]
param(
	[string]$BuildRoot = ".\build",
	[string]$BuildType = "Release",
	[string]$StageRoot = ""
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

function Get-CMakeExecutable {
	$command = Get-Command cmake.exe -ErrorAction SilentlyContinue
	if (-not $command) {
		$command = Get-Command cmake -ErrorAction SilentlyContinue
	}

	if ($command) {
		return $command.Source
	}

	$candidates = New-Object System.Collections.Generic.List[string]
	$candidates.Add((Join-Path ${env:ProgramFiles} "CMake\bin\cmake.exe"))

	foreach ($vsMajorVersion in @("18", "17")) {
		foreach ($edition in @("Enterprise", "Professional", "Community", "BuildTools")) {
			$candidates.Add((Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\$vsMajorVersion\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"))
		}
	}

	foreach ($candidate in $candidates) {
		if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
			return $candidate
		}
	}

	throw "Unable to locate cmake.exe. Ensure CMake is installed or available on PATH before staging the Windows client payload."
}

function Copy-DirectoryContents {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Source,

		[Parameter(Mandatory = $true)]
		[string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source)) {
		return
	}

	New-Item -ItemType Directory -Force -Path $Destination | Out-Null
	Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Copy-FileIfExists {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Source,

		[Parameter(Mandatory = $true)]
		[string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source)) {
		return
	}

	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
	Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Get-GStreamerRuntimeRoot {
	foreach ($envName in @("MUMBLE_GSTREAMER_ROOT", "GSTREAMER_1_0_ROOT_X86_64", "GSTREAMER_1_0_ROOT_MSVC_X86_64", "GSTREAMER_ROOT_X86_64")) {
		$value = [System.Environment]::GetEnvironmentVariable($envName, "Process")
		if (-not [string]::IsNullOrWhiteSpace($value) -and (Test-Path -LiteralPath $value)) {
			return (Resolve-Path -LiteralPath $value).Path
		}
	}

	return $null
}

$buildRootPath = Resolve-ExistingPath -Path $BuildRoot
$stageRootPath = if ([string]::IsNullOrWhiteSpace($StageRoot)) {
	Join-Path $buildRootPath "windows-client-payload"
} else {
	$StageRoot
}

if (Test-Path -LiteralPath $stageRootPath) {
	Remove-Item -LiteralPath $stageRootPath -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $stageRootPath | Out-Null

$cmakeExecutable = Get-CMakeExecutable
Write-Host "Using cmake from '$cmakeExecutable'."

try {
	& $cmakeExecutable --install $buildRootPath --config $BuildType --prefix $stageRootPath
	if ($LASTEXITCODE -ne 0) {
		throw "cmake --install failed while staging the Windows client payload."
	}
} catch {
	if (Test-Path -LiteralPath $stageRootPath) {
		Remove-Item -LiteralPath $stageRootPath -Recurse -Force
	}
	throw
}

# `cmake --install` covers most of the runtime tree, but some CI payload bits are
# produced next to the binaries in the build root and are safer to mirror here so
# the artifact becomes self-contained for local bring-up.
Copy-DirectoryContents -Source (Join-Path $buildRootPath "licenses") -Destination (Join-Path $stageRootPath "licenses")
Copy-DirectoryContents -Source (Join-Path $buildRootPath "dtln") -Destination (Join-Path $stageRootPath "dtln")
Copy-DirectoryContents -Source (Join-Path $buildRootPath "rnnoise") -Destination (Join-Path $stageRootPath "rnnoise")
Copy-DirectoryContents -Source (Join-Path $buildRootPath "deepfilternet") -Destination (Join-Path $stageRootPath "deepfilternet")

Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble.exe") -Destination (Join-Path $stageRootPath "mumble.exe")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble-server.exe") -Destination (Join-Path $stageRootPath "mumble-server.exe")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble-updater.exe") -Destination (Join-Path $stageRootPath "mumble-updater.exe")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble-screen-helper.exe") -Destination (Join-Path $stageRootPath "mumble-screen-helper.exe")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble_ol.dll") -Destination (Join-Path $stageRootPath "mumble_ol.dll")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble_ol_x64.dll") -Destination (Join-Path $stageRootPath "mumble_ol_x64.dll")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble_ol_helper.exe") -Destination (Join-Path $stageRootPath "mumble_ol_helper.exe")
Copy-FileIfExists -Source (Join-Path $buildRootPath "mumble_ol_helper_x64.exe") -Destination (Join-Path $stageRootPath "mumble_ol_helper_x64.exe")
Copy-FileIfExists -Source (Join-Path $buildRootPath "speexdsp.dll") -Destination (Join-Path $stageRootPath "speexdsp.dll")
Copy-FileIfExists -Source (Join-Path $buildRootPath "zlib1.dll") -Destination (Join-Path $stageRootPath "zlib1.dll")
Copy-FileIfExists -Source (Join-Path $buildRootPath "rnnoise.dll") -Destination (Join-Path $stageRootPath "rnnoise.dll")
Copy-FileIfExists -Source (Join-Path $buildRootPath "deepfilter.dll") -Destination (Join-Path $stageRootPath "deepfilter.dll")
Copy-FileIfExists -Source (Join-Path $buildRootPath "onnxruntime.dll") -Destination (Join-Path $stageRootPath "onnxruntime.dll")

$gstreamerRoot = Get-GStreamerRuntimeRoot
if ($gstreamerRoot) {
	Write-Host "Staging optional GStreamer runtime from '$gstreamerRoot'."
	$stageGStreamerRoot = Join-Path $stageRootPath "gstreamer"
	Copy-DirectoryContents -Source (Join-Path $gstreamerRoot "bin") -Destination (Join-Path $stageGStreamerRoot "bin")
	Copy-DirectoryContents -Source (Join-Path $gstreamerRoot "lib\gstreamer-1.0") -Destination (Join-Path $stageGStreamerRoot "lib\gstreamer-1.0")
	Copy-DirectoryContents -Source (Join-Path $gstreamerRoot "libexec") -Destination (Join-Path $stageGStreamerRoot "libexec")
}

Write-Host "Staged Windows client payload at '$stageRootPath'."
