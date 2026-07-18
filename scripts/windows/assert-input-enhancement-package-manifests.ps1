[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$StageRoot,

	[string]$ModelManifestPath = "",
	[string]$RecipeManifestPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

if (-not (Test-Path -LiteralPath $StageRoot -PathType Container)) {
	throw "Stage root does not exist: '$StageRoot'."
}
$stageRootPath = (Resolve-Path -LiteralPath $StageRoot).Path.TrimEnd('\', '/')
$resolvedModelManifest = if ([string]::IsNullOrWhiteSpace($ModelManifestPath)) {
	Join-Path $stageRootPath "input-models.json"
} else {
	$ModelManifestPath
}
$resolvedRecipeManifest = if ([string]::IsNullOrWhiteSpace($RecipeManifestPath)) {
	Join-Path $stageRootPath "input-recipes.json"
} else {
	$RecipeManifestPath
}
$modelManifest = Read-ReleaseJson -Path $resolvedModelManifest
$recipeManifest = Read-ReleaseJson -Path $resolvedRecipeManifest

if ([int](Assert-ObjectProperty -Object $modelManifest -Name "schemaVersion" -Context "Model manifest") -ne 1 -or
	[int](Assert-ObjectProperty -Object $recipeManifest -Name "schemaVersion" -Context "Recipe manifest") -ne 2) {
	throw "Packaged input-enhancement manifests must use model schemaVersion 1 and recipe schemaVersion 2."
}
$catalogRevision = [string](Assert-ObjectProperty -Object $modelManifest -Name "catalogRevision" -Context "Model manifest")
if ([string]::IsNullOrWhiteSpace($catalogRevision) -or
	[string](Assert-ObjectProperty -Object $recipeManifest -Name "catalogRevision" -Context "Recipe manifest") -cne $catalogRevision) {
	throw "Packaged model and recipe manifests must have the same non-empty catalogRevision."
}
$reportedModelHash = [string](Assert-ObjectProperty -Object $recipeManifest -Name "modelManifestSha256" -Context "Recipe manifest")
$actualModelHash = Get-ReleaseFileSha256 -Path $resolvedModelManifest
if ($reportedModelHash -cne $actualModelHash) {
	throw "Recipe manifest modelManifestSha256 does not match the packaged model manifest."
}

$modelIds = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::Ordinal)
$manifestAssetPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$modelCompatibility = @{}
$models = @(Assert-ObjectProperty -Object $modelManifest -Name "models" -Context "Model manifest")
if ($models.Count -eq 0) {
	throw "Packaged model manifest contains no models."
}
foreach ($model in $models) {
	$id = [string](Assert-ObjectProperty -Object $model -Name "id" -Context "Model manifest entry")
	if ([string]::IsNullOrWhiteSpace($id) -or -not $modelIds.Add($id)) {
		throw "Packaged model IDs must be non-empty and unique; invalid ID '$id'."
	}
	foreach ($requiredString in @("version", "backend", "licenseSpdx")) {
		if ([string]::IsNullOrWhiteSpace([string](Assert-ObjectProperty -Object $model -Name $requiredString -Context "Model '$id'"))) {
			throw "Model '$id' property '$requiredString' must not be empty."
		}
	}
	if ([int](Assert-ObjectProperty -Object $model -Name "sampleRateHz" -Context "Model '$id'") -le 0 -or
		[double](Assert-ObjectProperty -Object $model -Name "algorithmicLatencyMs" -Context "Model '$id'") -lt 0) {
		throw "Model '$id' has invalid sample rate or latency."
	}

	$relativePath = Assert-SafeRelativeReleasePath `
		-Path ([string](Assert-ObjectProperty -Object $model -Name "path" -Context "Model '$id'")) `
		-Context "Model '$id' path"
	if (-not $manifestAssetPaths.Add($relativePath)) {
		throw "Multiple models reference the same staged asset '$relativePath'."
	}
	$assetPath = Join-Path $stageRootPath ($relativePath -replace '/', '\')
	if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
		throw "Model '$id' asset is missing: '$relativePath'."
	}
	$resolvedAsset = (Resolve-Path -LiteralPath $assetPath).Path
	if (-not $resolvedAsset.StartsWith("$stageRootPath\", [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Model '$id' resolves outside the staged payload."
	}
	$actualHash = Get-ReleaseFileSha256 -Path $resolvedAsset
	$reportedHash = [string](Assert-ObjectProperty -Object $model -Name "sha256" -Context "Model '$id'")
	if ($reportedHash -cne $actualHash) {
		throw "Model '$id' SHA256 mismatch. Reported '$reportedHash', actual '$actualHash'."
	}
	if ([int64](Assert-ObjectProperty -Object $model -Name "size" -Context "Model '$id'") -ne [int64](Get-Item -LiteralPath $resolvedAsset).Length) {
		throw "Model '$id' size does not match its staged asset."
	}
	$modelCompatibility[$id] = @((Assert-ObjectProperty -Object $model -Name "recipeCompatibility" -Context "Model '$id'"))
}

$stagedModelFiles = New-Object System.Collections.Generic.List[string]
foreach ($directory in @("rnnoise", "dtln", "deepfilternet")) {
	$root = Join-Path $stageRootPath $directory
	if (-not (Test-Path -LiteralPath $root -PathType Container)) {
		continue
	}
	foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
		if ($file.Name -match '\.(?:bin|blob|onnx|gz|tar)$') {
			$relative = $file.FullName.Substring($stageRootPath.Length).TrimStart('\', '/') -replace '\\', '/'
			$stagedModelFiles.Add($relative)
		}
	}
}
$unmanifested = @($stagedModelFiles | Where-Object { -not $manifestAssetPaths.Contains($_) })
if ($unmanifested.Count -gt 0) {
	throw "Staged model assets are missing from input-models.json: $($unmanifested -join ', ')."
}

$allowedProfiles = @("Original", "Light", "Balanced", "Quality", "VoiceFocus", "Auto")
$seenProfiles = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::Ordinal)
$recipeIds = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::Ordinal)
$recipes = @(Assert-ObjectProperty -Object $recipeManifest -Name "recipes" -Context "Recipe manifest")
foreach ($recipe in $recipes) {
	$id = [string](Assert-ObjectProperty -Object $recipe -Name "id" -Context "Recipe manifest entry")
	if ([string]::IsNullOrWhiteSpace($id) -or -not $recipeIds.Add($id)) {
		throw "Packaged recipe IDs must be non-empty and unique; invalid ID '$id'."
	}
	$profile = [string](Assert-ObjectProperty -Object $recipe -Name "profile" -Context "Recipe '$id'")
	if ($allowedProfiles -cnotcontains $profile) {
		throw "Recipe '$id' has unsupported profile '$profile'."
	}
	$null = $seenProfiles.Add($profile)
	foreach ($modelId in @((Assert-ObjectProperty -Object $recipe -Name "modelIds" -Context "Recipe '$id'"))) {
		$modelIdString = [string]$modelId
		if (-not $modelIds.Contains($modelIdString)) {
			throw "Recipe '$id' references unknown model '$modelIdString'."
		}
		if (@($modelCompatibility[$modelIdString] | Where-Object { [string]$_ -ceq $id }).Count -ne 1) {
			throw "Model '$modelIdString' does not declare compatibility with recipe '$id'."
		}
	}
}
foreach ($profile in $allowedProfiles) {
	if (-not $seenProfiles.Contains($profile)) {
		throw "Packaged recipes do not contain the required '$profile' profile."
	}
}
foreach ($modelId in $modelIds) {
	foreach ($compatibleRecipe in @($modelCompatibility[$modelId])) {
		if (-not $recipeIds.Contains([string]$compatibleRecipe)) {
			throw "Model '$modelId' declares unknown compatible recipe '$compatibleRecipe'."
		}
	}
}

Write-Host "Verified $($models.Count) staged model asset(s), $($recipes.Count) recipe(s), and catalog revision '$catalogRevision'."
