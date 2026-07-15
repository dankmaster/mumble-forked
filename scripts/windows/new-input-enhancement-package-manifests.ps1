[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$StageRoot,

	[Parameter(Mandatory = $true)]
	[string]$ModelDescriptorPath,

	[Parameter(Mandatory = $true)]
	[string]$RecipeDescriptorPath,

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
$modelDescriptor = Read-ReleaseJson -Path $ModelDescriptorPath
$recipeDescriptor = Read-ReleaseJson -Path $RecipeDescriptorPath

if ([int](Assert-ObjectProperty -Object $modelDescriptor -Name "schemaVersion" -Context "Model descriptor") -ne 1) {
	throw "Model descriptor schemaVersion must be 1."
}
if ([int](Assert-ObjectProperty -Object $recipeDescriptor -Name "schemaVersion" -Context "Recipe descriptor") -ne 1) {
	throw "Recipe descriptor schemaVersion must be 1."
}

$catalogRevision = [string](Assert-ObjectProperty -Object $modelDescriptor -Name "catalogRevision" -Context "Model descriptor")
if ([string]::IsNullOrWhiteSpace($catalogRevision)) {
	throw "Model descriptor catalogRevision must not be empty."
}
if ([string](Assert-ObjectProperty -Object $recipeDescriptor -Name "catalogRevision" -Context "Recipe descriptor") -cne $catalogRevision) {
	throw "Model and recipe descriptors must use the same catalogRevision."
}

$modelIds = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::Ordinal)
$packagedModels = New-Object System.Collections.Generic.List[object]
foreach ($model in @(Assert-ObjectProperty -Object $modelDescriptor -Name "models" -Context "Model descriptor")) {
	$id = [string](Assert-ObjectProperty -Object $model -Name "id" -Context "Model descriptor entry")
	if ([string]::IsNullOrWhiteSpace($id) -or -not $modelIds.Add($id)) {
		throw "Model descriptor IDs must be non-empty and unique; invalid ID '$id'."
	}

	$relativePath = Assert-SafeRelativeReleasePath `
		-Path ([string](Assert-ObjectProperty -Object $model -Name "path" -Context "Model '$id'")) `
		-Context "Model '$id' path"
	$assetPath = Join-Path $stageRootPath ($relativePath -replace '/', '\')
	if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
		throw "Model '$id' asset is missing from the staged payload: '$relativePath'."
	}
	$resolvedAsset = (Resolve-Path -LiteralPath $assetPath).Path
	if (-not $resolvedAsset.StartsWith("$stageRootPath\", [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "Model '$id' resolves outside the staged payload."
	}

	$packagedModels.Add([ordered]@{
		id                  = $id
		version             = [string](Assert-ObjectProperty -Object $model -Name "version" -Context "Model '$id'")
		backend             = [string](Assert-ObjectProperty -Object $model -Name "backend" -Context "Model '$id'")
		path                = $relativePath
		sha256              = Get-ReleaseFileSha256 -Path $resolvedAsset
		size                = [int64](Get-Item -LiteralPath $resolvedAsset).Length
		licenseSpdx         = [string](Assert-ObjectProperty -Object $model -Name "licenseSpdx" -Context "Model '$id'")
		sampleRateHz        = [int](Assert-ObjectProperty -Object $model -Name "sampleRateHz" -Context "Model '$id'")
		algorithmicLatencyMs = [double](Assert-ObjectProperty -Object $model -Name "algorithmicLatencyMs" -Context "Model '$id'")
		recipeCompatibility = @((Assert-ObjectProperty -Object $model -Name "recipeCompatibility" -Context "Model '$id'"))
	})
}
if ($packagedModels.Count -eq 0) {
	throw "Model descriptor must contain at least one staged model."
}

$modelManifest = [ordered]@{
	schemaVersion       = 1
	catalogRevision     = $catalogRevision
	generatedFromAssets = $true
	models              = $packagedModels.ToArray()
}
$resolvedModelManifestPath = if ([string]::IsNullOrWhiteSpace($ModelManifestPath)) {
	Join-Path $stageRootPath "input-models.json"
} else {
	$ModelManifestPath
}
Write-ReleaseJson -Value $modelManifest -Path $resolvedModelManifestPath
$modelManifestHash = Get-ReleaseFileSha256 -Path $resolvedModelManifestPath

$allowedProfiles = @("Original", "Light", "Balanced", "Crisp", "Auto")
$seenProfiles = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::Ordinal)
$recipeIds = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::Ordinal)
$packagedRecipes = New-Object System.Collections.Generic.List[object]
foreach ($recipe in @(Assert-ObjectProperty -Object $recipeDescriptor -Name "recipes" -Context "Recipe descriptor")) {
	$id = [string](Assert-ObjectProperty -Object $recipe -Name "id" -Context "Recipe descriptor entry")
	if ([string]::IsNullOrWhiteSpace($id) -or -not $recipeIds.Add($id)) {
		throw "Recipe IDs must be non-empty and unique; invalid ID '$id'."
	}

	$profile = [string](Assert-ObjectProperty -Object $recipe -Name "profile" -Context "Recipe '$id'")
	if ($allowedProfiles -cnotcontains $profile) {
		throw "Recipe '$id' has unsupported profile '$profile'."
	}
	$null = $seenProfiles.Add($profile)

	$referencedModels = @((Assert-ObjectProperty -Object $recipe -Name "modelIds" -Context "Recipe '$id'"))
	foreach ($modelId in $referencedModels) {
		if (-not $modelIds.Contains([string]$modelId)) {
			throw "Recipe '$id' references unknown model '$modelId'."
		}
	}

	$packagedRecipes.Add($recipe)
}
foreach ($profile in $allowedProfiles) {
	if (-not $seenProfiles.Contains($profile)) {
		throw "Recipe descriptor does not contain a '$profile' product profile."
	}
}

$recipeManifest = [ordered]@{
	schemaVersion       = 1
	catalogRevision     = $catalogRevision
	modelManifestSha256 = $modelManifestHash
	recipes             = $packagedRecipes.ToArray()
}
$resolvedRecipeManifestPath = if ([string]::IsNullOrWhiteSpace($RecipeManifestPath)) {
	Join-Path $stageRootPath "input-recipes.json"
} else {
	$RecipeManifestPath
}
Write-ReleaseJson -Value $recipeManifest -Path $resolvedRecipeManifestPath

Write-Host "Wrote staged model manifest '$resolvedModelManifestPath' from actual asset hashes."
Write-Host "Wrote staged recipe manifest '$resolvedRecipeManifestPath'."
