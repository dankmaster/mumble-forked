[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$MsiPath,

	[Parameter(Mandatory = $true)]
	[string]$QualifiedPayloadRoot,

	[string]$AdministrativeImageRoot = "",

	[string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "InputEnhancementReleaseTools.psm1") -Force

function Get-ManagedPayloadFileMap {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)

	if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
		throw "$Context root does not exist: '$Root'."
	}
	$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
	$files = New-Object 'System.Collections.Generic.Dictionary[string,object]' `
		([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($file in @(Get-ChildItem -LiteralPath $rootPath -Recurse -File | Sort-Object -Property FullName)) {
		$relativePath = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/').Replace('\', '/')
		$relativePath = Assert-SafeRelativeReleasePath -Path $relativePath -Context "$Context file path"
		if ($files.ContainsKey($relativePath)) {
			throw "$Context contains duplicate case-insensitive path '$relativePath'."
		}
		$files.Add($relativePath, $file)
	}
	if ($files.Count -eq 0) {
		throw "$Context contains no managed payload files."
	}
	return [pscustomobject]@{ root = $rootPath; files = $files }
}

$msi = Get-Item -LiteralPath $MsiPath -ErrorAction Stop
if ($msi.PSIsContainer -or $msi.Extension -cne '.msi') {
	throw "MSI payload verification requires a .msi file."
}
$qualifiedRoot = (Resolve-Path -LiteralPath $QualifiedPayloadRoot -ErrorAction Stop).Path.TrimEnd('\', '/')
$temporaryRoot = ""
$method = "pre-extracted-administrative-image"

try {
	if ([string]::IsNullOrWhiteSpace($AdministrativeImageRoot)) {
		if ($env:OS -cne 'Windows_NT') {
			throw "MSI administrative extraction is supported only on Windows."
		}
		$method = "msiexec-administrative-image"
		$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
			("mumble-msi-admin-" + [guid]::NewGuid().ToString('N'))
		$administrativeRoot = Join-Path $temporaryRoot 'image'
		$logPath = Join-Path $temporaryRoot 'msiexec.log'
		New-Item -ItemType Directory -Force -Path $administrativeRoot | Out-Null
		$msiexecPath = Join-Path $env:SystemRoot 'System32\msiexec.exe'
		if (-not (Test-Path -LiteralPath $msiexecPath -PathType Leaf)) {
			throw "Unable to locate msiexec.exe for administrative extraction."
		}
		$argumentLine = "/a `"$($msi.FullName)`" /qn TARGETDIR=`"$administrativeRoot`" /L*v `"$logPath`""
		$process = Start-Process -FilePath $msiexecPath -ArgumentList $argumentLine -PassThru
		if (-not $process.WaitForExit(180000)) {
			try { $process.Kill() } catch { }
			throw "MSI administrative extraction exceeded the three-minute timeout."
		}
		$process.Refresh()
		if ($process.ExitCode -ne 0) {
			$logTail = if (Test-Path -LiteralPath $logPath -PathType Leaf) {
				@(Get-Content -LiteralPath $logPath -Tail 20) -join " | "
			} else { "no msiexec log was produced" }
			throw "MSI administrative extraction failed with exit code $($process.ExitCode): $logTail"
		}
	} else {
		$administrativeRoot = (Resolve-Path -LiteralPath $AdministrativeImageRoot -ErrorAction Stop).Path.TrimEnd('\', '/')
	}

	$qualifiedClientPath = Join-Path $qualifiedRoot 'mumble.exe'
	$qualifiedClientHash = Get-ReleaseFileSha256 -Path $qualifiedClientPath
	$payloadCandidates = @(Get-ChildItem -LiteralPath $administrativeRoot -Recurse -File -Filter 'mumble.exe' |
		Where-Object { (Get-ReleaseFileSha256 -Path $_.FullName) -ceq $qualifiedClientHash })
	if ($payloadCandidates.Count -ne 1) {
		throw "Administrative MSI image must contain exactly one mumble.exe matching the qualified payload; found $($payloadCandidates.Count)."
	}
	$installedPayloadRoot = $payloadCandidates[0].Directory.FullName.TrimEnd('\', '/')
	$qualifiedPayload = Get-ManagedPayloadFileMap -Root $qualifiedRoot -Context 'Qualified payload'
	$installedPayload = Get-ManagedPayloadFileMap -Root $installedPayloadRoot -Context 'Administrative MSI payload'
	$missingPaths = @($qualifiedPayload.files.Keys | Where-Object { -not $installedPayload.files.ContainsKey($_) } | Sort-Object)
	$unexpectedPaths = @($installedPayload.files.Keys | Where-Object { -not $qualifiedPayload.files.ContainsKey($_) } | Sort-Object)
	if ($missingPaths.Count -gt 0 -or $unexpectedPaths.Count -gt 0) {
		throw "MSI managed payload file-set mismatch. Missing: [$($missingPaths -join ', ')]; unexpected: [$($unexpectedPaths -join ', ')]."
	}

	$verifiedFiles = New-Object System.Collections.Generic.List[object]
	foreach ($relativePath in @($qualifiedPayload.files.Keys | Sort-Object)) {
		$qualifiedFile = $qualifiedPayload.files[$relativePath]
		$installedFile = $installedPayload.files[$relativePath]
		$qualifiedHash = Get-ReleaseFileSha256 -Path $qualifiedFile.FullName
		$installedHash = Get-ReleaseFileSha256 -Path $installedFile.FullName
		if ($qualifiedFile.PSIsContainer -or $installedFile.PSIsContainer -or
			[int64]$qualifiedFile.Length -ne [int64]$installedFile.Length -or
			$qualifiedHash -cne $installedHash) {
			throw "MSI administrative image payload '$relativePath' does not match the qualified payload bytes."
		}
		$verifiedFiles.Add([ordered]@{
			path = $relativePath.Replace('\', '/')
			sha256 = $qualifiedHash
			size = [int64]$qualifiedFile.Length
		})
	}

	# An administrative image may place only its source database and external
	# cabinets at TARGETDIR's root. Windows Installer owns uninstall metadata in
	# its database/registry; this MSI intentionally generates no app-root
	# uninstaller files. Every file below the application payload root is managed
	# and therefore already required to match the qualified payload exactly.
	$administrativeArtifacts = New-Object System.Collections.Generic.List[object]
	$unexpectedAdministrativeArtifacts = New-Object System.Collections.Generic.List[string]
	$payloadPrefix = "$installedPayloadRoot\"
	foreach ($file in @(Get-ChildItem -LiteralPath $administrativeRoot -Recurse -File | Sort-Object -Property FullName)) {
		if ($file.FullName.StartsWith($payloadPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
			continue
		}
		$relativePath = $file.FullName.Substring($administrativeRoot.Length).TrimStart('\', '/').Replace('\', '/')
		$isRootArtifact = -not $relativePath.Contains('/')
		$isDocumentedMetadata = $isRootArtifact -and $file.Extension -in @('.msi', '.cab')
		if (-not $isDocumentedMetadata) {
			$unexpectedAdministrativeArtifacts.Add($relativePath)
			continue
		}
		$administrativeArtifacts.Add([ordered]@{
			path = $relativePath
			kind = if ($file.Extension -eq '.msi') { 'administrative-database' } else { 'external-cabinet' }
			sha256 = Get-ReleaseFileSha256 -Path $file.FullName
			size = [int64]$file.Length
		})
	}
	if ($unexpectedAdministrativeArtifacts.Count -gt 0) {
		throw "MSI administrative image contains undocumented files outside the managed payload: [$($unexpectedAdministrativeArtifacts -join ', ')]."
	}

	if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
		$relativeInstalledRoot = $installedPayloadRoot.Substring($administrativeRoot.Length).TrimStart('\', '/')
		Write-ReleaseJson -Path $OutputPath -Value ([ordered]@{
			schemaVersion = 1
			passed = $true
			method = $method
			msi = [ordered]@{
				fileName = $msi.Name
				sha256 = Get-ReleaseFileSha256 -Path $msi.FullName
				size = [int64]$msi.Length
			}
			administrativePayloadRoot = $relativeInstalledRoot.Replace('\', '/')
			verifiedFileCount = $verifiedFiles.Count
			files = $verifiedFiles.ToArray()
			allowedAdministrativeArtifactCount = $administrativeArtifacts.Count
			allowedAdministrativeArtifacts = $administrativeArtifacts.ToArray()
		})
	}
} finally {
	if (-not [string]::IsNullOrWhiteSpace($temporaryRoot)) {
		Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
	}
}

Write-Host "Verified complete MSI payload parity for $($verifiedFiles.Count) managed file(s)."
