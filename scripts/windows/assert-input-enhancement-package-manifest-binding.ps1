[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$ModelManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeManifestPath,

	[Parameter(Mandatory = $true)]
	[string]$ModelManifestSignaturePath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeManifestSignaturePath,

	[string]$ExpandedPayloadRoot = "",

	[string]$UpdatePackagePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

$hasExpandedPayload = -not [string]::IsNullOrWhiteSpace($ExpandedPayloadRoot)
$hasUpdatePackage = -not [string]::IsNullOrWhiteSpace($UpdatePackagePath)
if ($hasExpandedPayload -eq $hasUpdatePackage) {
	throw "Specify exactly one of ExpandedPayloadRoot or UpdatePackagePath."
}

$temporaryRoot = ""
try {
	if ($hasUpdatePackage) {
		$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
			("mumble-update-manifest-binding-" + [guid]::NewGuid().ToString('N'))
		& (Join-Path $PSScriptRoot "assert-windows-update-package.ps1") `
			-PackagePath $UpdatePackagePath `
			-ExpandedPayloadPath $temporaryRoot
		$payloadRoot = $temporaryRoot
	} else {
		$payloadRoot = (Resolve-Path -LiteralPath $ExpandedPayloadRoot -ErrorAction Stop).Path
	}

	$bindings = @(
		@("input-models.json", $ModelManifestPath),
		@("input-models.json.sig", $ModelManifestSignaturePath),
		@("input-recipes.json", $RecipeManifestPath),
		@("input-recipes.json.sig", $RecipeManifestSignaturePath)
	)
	foreach ($binding in $bindings) {
		$relativeName = [string]$binding[0]
		$qualifiedPath = [string]$binding[1]
		$qualifiedFile = Get-Item -LiteralPath $qualifiedPath -ErrorAction Stop
		$packagedPath = Join-Path $payloadRoot $relativeName
		$packagedFile = Get-Item -LiteralPath $packagedPath -ErrorAction Stop
		if ($qualifiedFile.PSIsContainer -or $packagedFile.PSIsContainer -or
			[int64]$qualifiedFile.Length -ne [int64]$packagedFile.Length -or
			(Get-ReleaseFileSha256 -Path $qualifiedFile.FullName) -cne
				(Get-ReleaseFileSha256 -Path $packagedFile.FullName)) {
			throw "Update payload '$relativeName' is not byte-identical to the qualified external file."
		}
	}
} finally {
	if (-not [string]::IsNullOrWhiteSpace($temporaryRoot)) {
		Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

Write-Host "Verified byte-exact update binding for both package manifests and detached signatures."
