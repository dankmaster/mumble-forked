$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$script:createPackage = Join-Path $script:repoRoot 'scripts\windows\create-windows-update-package.ps1'
$script:assertPackage = Join-Path $script:repoRoot 'scripts\windows\assert-windows-update-package.ps1'

function Write-TestFile {
	param(
		[Parameter(Mandatory = $true)]
		[string] $Path,
		[Parameter(Mandatory = $true)]
		[string] $Content
	)
	$parent = Split-Path -Parent $Path
	if (-not [string]::IsNullOrWhiteSpace($parent)) {
		New-Item -ItemType Directory -Force -Path $parent | Out-Null
	}
	[IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function Get-TestRelativePath {
	param([string] $Root, [string] $Path)
	$rootPath = [IO.Path]::GetFullPath($Root)
	if (-not $rootPath.EndsWith([IO.Path]::DirectorySeparatorChar)) {
		$rootPath += [IO.Path]::DirectorySeparatorChar
	}
	return [IO.Path]::GetFullPath($Path).Substring($rootPath.Length).Replace('\', '/')
}

function Write-TestRuntimeManifest {
	param([string] $Root)
	$manifestPath = Join-Path $Root 'runtime-manifest.json'
	$runtimeFiles = @(Get-ChildItem -LiteralPath $Root -Recurse -File |
		Where-Object { $_.FullName -ne $manifestPath } |
		Sort-Object { Get-TestRelativePath -Root $Root -Path $_.FullName })
	$records = @($runtimeFiles | ForEach-Object {
		[ordered]@{
			path = Get-TestRelativePath -Root $Root -Path $_.FullName
			size = [int64]$_.Length
			sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
		}
	})
	$document = [ordered]@{
		schema_version = 1
		files = $records
	}
	Write-TestFile -Path $manifestPath -Content (($document | ConvertTo-Json -Depth 8) + "`n")
}

function New-TestQtQuickStage {
	param([string] $Root)
	$tokens = $null
	$parseErrors = $null
	$ast = [Management.Automation.Language.Parser]::ParseFile(
		$script:createPackage,
		[ref]$tokens,
		[ref]$parseErrors
	)
	if ($parseErrors.Count -ne 0) {
		throw "Could not parse create-windows-update-package.ps1."
	}
	$function = $ast.Find(
		{
			param($node)
			$node -is [Management.Automation.Language.FunctionDefinitionAst] -and
				$node.Name -eq 'Get-RequiredQtQuickPayloadPaths'
		},
		$true
	)
	if ($null -eq $function) {
		throw "Could not find Get-RequiredQtQuickPayloadPaths."
	}
	. ([scriptblock]::Create($function.Extent.Text))

	New-Item -ItemType Directory -Force -Path $Root | Out-Null
	foreach ($relativePath in @(Get-RequiredQtQuickPayloadPaths)) {
		Write-TestFile -Path (Join-Path $Root $relativePath) -Content "fixture:$relativePath"
	}
	Write-TestFile -Path (Join-Path $Root 'direct-runtime-dependencies.txt') `
		-Content "Qt6Quick.dll`nQt6Qml.dll`n"
	Write-TestFile -Path (Join-Path $Root 'delay-load-runtime-dependencies.txt') `
		-Content "Qt6WebEngineCore.dll`nQt6WebEngineQuick.dll`n"
	Write-TestRuntimeManifest -Root $Root
}

function Assert-Throws {
	param(
		[Parameter(Mandatory = $true)]
		[scriptblock] $Script,
		[Parameter(Mandatory = $true)]
		[string] $Description
	)
	$threw = $false
	try {
		& $Script
	} catch {
		$threw = $true
	}
	$threw | Should Be $true
}

function Expand-TestPackage {
	param([string] $PackagePath, [string] $Destination)
	$zipPath = "$Destination.zip"
	Copy-Item -LiteralPath $PackagePath -Destination $zipPath -Force
	Expand-Archive -LiteralPath $zipPath -DestinationPath $Destination -Force
	Remove-Item -LiteralPath $zipPath -Force
}

function Compress-TestPackage {
	param([string] $Source, [string] $PackagePath)
	$zipPath = "$PackagePath.zip"
	Remove-Item -LiteralPath $PackagePath, $zipPath -Force -ErrorAction SilentlyContinue
	Compress-Archive -LiteralPath (Join-Path $Source 'manifest.json'), (Join-Path $Source 'payload') `
		-DestinationPath $zipPath -CompressionLevel Optimal
	Move-Item -LiteralPath $zipPath -Destination $PackagePath
}

Describe 'Sparse Windows update package contract' {
	BeforeEach {
		$script:caseRoot = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
		$script:baseStage = Join-Path $script:caseRoot 'base-stage'
		$script:targetStage = Join-Path $script:caseRoot 'target-stage'
		$script:basePackage = Join-Path $script:caseRoot 'base.mumble-update'
		$script:sparsePackage = Join-Path $script:caseRoot 'sparse.mumble-update'
		$script:baseManifest = Join-Path $script:caseRoot 'base-files.json'
		$script:targetManifest = Join-Path $script:caseRoot 'target-files.json'
		$script:baseMetadata = Join-Path $script:caseRoot 'base-metadata.json'
		$script:sparseMetadata = Join-Path $script:caseRoot 'sparse-metadata.json'
		$script:baseCommit = '1111111111111111111111111111111111111111'
		$script:targetCommit = '2222222222222222222222222222222222222222'

		New-TestQtQuickStage -Root $script:baseStage
		Write-TestFile -Path (Join-Path $script:baseStage 'obsolete-runtime.dll') -Content 'obsolete'
		Write-TestRuntimeManifest -Root $script:baseStage

		& $script:createPackage -StageRoot $script:baseStage -OutputPath $script:basePackage `
			-Version '1.7.84' -Build 84 -Commit $script:baseCommit `
			-ManifestOutPath $script:baseMetadata -TargetManifestOutPath $script:baseManifest -Validate

		New-Item -ItemType Directory -Force -Path $script:targetStage | Out-Null
		Copy-Item -Path (Join-Path $script:baseStage '*') -Destination $script:targetStage -Recurse -Force
		Write-TestFile -Path (Join-Path $script:targetStage 'mumble.exe') -Content 'changed-client'
		Write-TestFile -Path (Join-Path $script:targetStage 'features\new-runtime.dat') -Content 'new-runtime'
		Remove-Item -LiteralPath (Join-Path $script:targetStage 'obsolete-runtime.dll') -Force
		Write-TestRuntimeManifest -Root $script:targetStage

		$script:baseManifestSha256 =
			(Get-FileHash -LiteralPath $script:baseManifest -Algorithm SHA256).Hash.ToLowerInvariant()
		& $script:createPackage -StageRoot $script:targetStage -OutputPath $script:sparsePackage `
			-Version '1.7.85' -Build 85 -Commit $script:targetCommit `
			-ManifestOutPath $script:sparseMetadata -TargetManifestOutPath $script:targetManifest `
			-BaseManifestPath $script:baseManifest `
			-ExpectedBaseManifestSha256 $script:baseManifestSha256 -Validate
	}

	It 'ships only changed and new files while retaining the complete target manifest' {
		$baseMetadata = Get-Content -LiteralPath $script:baseMetadata -Raw | ConvertFrom-Json
		$sparseMetadata = Get-Content -LiteralPath $script:sparseMetadata -Raw | ConvertFrom-Json
		$targetManifest = Get-Content -LiteralPath $script:targetManifest -Raw | ConvertFrom-Json
		$baseMetadata.payloadMode | Should Be 'full'
		[int]$baseMetadata.payloadFileCount | Should Be ([int]$baseMetadata.targetFileCount)
		$sparseMetadata.payloadMode | Should Be 'sparse'
		[int]$sparseMetadata.payloadFileCount | Should Be 3
		[int]$sparseMetadata.removedFileCount | Should Be 1
		[int]$sparseMetadata.targetFileCount | Should Be @($targetManifest.files).Count

		$expanded = Join-Path $script:caseRoot 'expanded'
		Expand-TestPackage -PackagePath $script:sparsePackage -Destination $expanded
		$payloadPaths = @(Get-ChildItem -LiteralPath (Join-Path $expanded 'payload') -Recurse -File |
			ForEach-Object {
				Get-TestRelativePath -Root (Join-Path $expanded 'payload') -Path $_.FullName
			} |
			Sort-Object)
		($payloadPaths -join ',') | Should Be 'features/new-runtime.dat,mumble.exe,runtime-manifest.json'
		@($targetManifest.files | Where-Object path -eq 'mumble-updater.exe').Count | Should Be 1
		@($targetManifest.files | Where-Object path -eq 'obsolete-runtime.dll').Count | Should Be 0
	}

	It 'verifies the exact base hash and the complete reconstructed target' {
		& $script:assertPackage -PackagePath $script:sparsePackage `
			-ExpectedCommit $script:targetCommit -ExpectedBuild 85 -ExpectedVersion '1.7.85' `
			-BaseManifestPath $script:baseManifest `
			-ExpectedBaseManifestSha256 $script:baseManifestSha256 `
			-TargetPayloadPath $script:targetStage

		Assert-Throws -Description 'wrong base manifest digest' -Script {
			& $script:assertPackage -PackagePath $script:sparsePackage `
				-ExpectedCommit $script:targetCommit -ExpectedBuild 85 -ExpectedVersion '1.7.85' `
				-BaseManifestPath $script:baseManifest `
				-ExpectedBaseManifestSha256 ('0' * 64) `
				-TargetPayloadPath $script:targetStage
		}
	}

	It 'rejects missing or tampered changed-file payloads' {
		$tamperedRoot = Join-Path $script:caseRoot 'tampered'
		Expand-TestPackage -PackagePath $script:sparsePackage -Destination $tamperedRoot
		Write-TestFile -Path (Join-Path $tamperedRoot 'payload\mumble.exe') -Content 'tampered-client'
		$tamperedPackage = Join-Path $script:caseRoot 'tampered.mumble-update'
		Compress-TestPackage -Source $tamperedRoot -PackagePath $tamperedPackage
		Assert-Throws -Description 'tampered changed file' -Script {
			& $script:assertPackage -PackagePath $tamperedPackage `
				-ExpectedCommit $script:targetCommit -ExpectedBuild 85 -ExpectedVersion '1.7.85' `
				-BaseManifestPath $script:baseManifest `
				-ExpectedBaseManifestSha256 $script:baseManifestSha256 `
				-TargetPayloadPath $script:targetStage
		}

		$missingRoot = Join-Path $script:caseRoot 'missing'
		Expand-TestPackage -PackagePath $script:sparsePackage -Destination $missingRoot
		Remove-Item -LiteralPath (Join-Path $missingRoot 'payload\features\new-runtime.dat') -Force
		$missingPackage = Join-Path $script:caseRoot 'missing.mumble-update'
		Compress-TestPackage -Source $missingRoot -PackagePath $missingPackage
		Assert-Throws -Description 'missing changed file' -Script {
			& $script:assertPackage -PackagePath $missingPackage `
				-ExpectedCommit $script:targetCommit -ExpectedBuild 85 -ExpectedVersion '1.7.85' `
				-BaseManifestPath $script:baseManifest `
				-ExpectedBaseManifestSha256 $script:baseManifestSha256 `
				-TargetPayloadPath $script:targetStage
		}
	}
}
