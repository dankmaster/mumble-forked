[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$UpdaterPath,

	[string]$DumpbinPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$updater = (Resolve-Path -LiteralPath $UpdaterPath -ErrorAction Stop).Path
if ([string]::IsNullOrWhiteSpace($DumpbinPath)) {
	$command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
	if ($command) {
		$DumpbinPath = $command.Source
	} else {
		$searchRoots = New-Object System.Collections.Generic.List[string]
		if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
			$searchRoots.Add((Join-Path $env:VSINSTALLDIR 'VC\Tools\MSVC'))
		}
		$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
		if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
			$installations = @(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
				-property installationPath -format value)
			foreach ($installation in $installations) {
				if (-not [string]::IsNullOrWhiteSpace($installation)) {
					$searchRoots.Add((Join-Path $installation.Trim() 'VC\Tools\MSVC'))
				}
			}
		}
		foreach ($root in $searchRoots) {
			$candidate = Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
				Sort-Object Name -Descending |
				ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
				Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
				Select-Object -First 1
			if ($candidate) {
				$DumpbinPath = $candidate
				break
			}
		}
	}
	if ([string]::IsNullOrWhiteSpace($DumpbinPath)) {
		throw "dumpbin.exe is required to verify mumble-updater.exe imports and was not found in Visual Studio."
	}
}
$dumpbin = (Resolve-Path -LiteralPath $DumpbinPath -ErrorAction Stop).Path
$output = @(& $dumpbin /dependents $updater 2>&1)
if ($LASTEXITCODE -ne 0) {
	throw "dumpbin /dependents failed for '$updater':`n$($output -join [Environment]::NewLine)"
}
$text = $output -join "`n"
if ($text -match '(?im)^\s*zlib1[.]dll\s*$') {
	throw "'$updater' imports zlib1.dll; the recovery updater must link its private /MT zlib statically."
}

Write-Host "Verified that '$updater' has no zlib1.dll dependency."
