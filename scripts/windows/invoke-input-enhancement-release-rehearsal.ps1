[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$SourceRoot,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$SourceSha,
	[Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int]$BuildNumber,
	[Parameter(Mandatory = $true)] [string]$ExecutorPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExecutorSha256,
	[Parameter(Mandatory = $true)] [string]$UnsignedHandoffArchivePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UnsignedHandoffArchiveSha256,
	[Parameter(Mandatory = $true)] [string]$MeasuredEvidenceArchivePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$MeasuredEvidenceArchiveSha256,
	[Parameter(Mandatory = $true)] [string]$ListeningQualificationPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ListeningQualificationSha256,
	[Parameter(Mandatory = $true)] [string]$ReleaseSmokeHarnessPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ReleaseSmokeHarnessSha256,
	[Parameter(Mandatory = $true)] [string]$FixtureManifestPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$FixtureManifestSha256,
	[Parameter(Mandatory = $true)] [string]$CaseSetPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$CaseSetSha256,
	[Parameter(Mandatory = $true)] [string]$ServerExecutablePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ServerExecutableSha256,
	[Parameter(Mandatory = $true)] [string]$KillSwitchObserverPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$KillSwitchObserverSha256,
	[Parameter(Mandatory = $true)] [string]$KillSwitchObserverReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$KillSwitchObserverReceiptSha256,
	[Parameter(Mandatory = $true)] [string]$UpdaterVmExecutorPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmExecutorSha256,
	[Parameter(Mandatory = $true)] [string]$UpdaterVmReceiptPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmReceiptSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmImageSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmSnapshotSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$UpdaterVmHardwareFingerprintSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$')] [string]$DraftArtifactName,
	[Parameter(Mandatory = $true)] [string]$OutputRoot,
	[ValidateRange(0, 1000)] [int]$CommunitySize = 0,
	[string]$OpenSslPath = '',
	[ValidatePattern('^https://[^\s]+$')] [string]$TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'Release rehearsal is Windows-only.' }
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force
$openssl = Resolve-InputEnhancementOpenSsl -OpenSslPath $OpenSslPath
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\', '/')
if ((git -C $sourceRootPath rev-parse HEAD).Trim() -cne $SourceSha) {
	throw 'Release rehearsal source checkout is not the requested immutable commit.'
}

$forbiddenEnvironment = @(Get-ChildItem Env: | Where-Object {
	$_.Name -match '^(AZURE_|ACTIONS_ID_TOKEN_REQUEST_|INPUT_ENHANCEMENT_ED25519_PRIVATE_KEY|AZURE_ARTIFACT_SIGNING|TRUSTED_SIGNING)' -or
	$_.Name -match '(?i)(PRODUCTION.*PRIVATE|PROD.*SIGNING.*KEY)'
})
if ($forbiddenEnvironment.Count -ne 0) {
	throw "Pre-Azure rehearsal refuses production/Azure/OIDC environment material: $($forbiddenEnvironment.Name -join ', ')."
}

$protectedInputs = @(
	@('executor', $ExecutorPath, $ExecutorSha256),
	@('unsigned handoff archive', $UnsignedHandoffArchivePath, $UnsignedHandoffArchiveSha256),
	@('measured evidence archive', $MeasuredEvidenceArchivePath, $MeasuredEvidenceArchiveSha256),
	@('listening qualification', $ListeningQualificationPath, $ListeningQualificationSha256),
	@('release-smoke harness', $ReleaseSmokeHarnessPath, $ReleaseSmokeHarnessSha256),
	@('fixture manifest', $FixtureManifestPath, $FixtureManifestSha256),
	@('case set', $CaseSetPath, $CaseSetSha256),
	@('OG server executable', $ServerExecutablePath, $ServerExecutableSha256),
	@('kill-switch observer', $KillSwitchObserverPath, $KillSwitchObserverSha256),
	@('kill-switch observer receipt', $KillSwitchObserverReceiptPath, $KillSwitchObserverReceiptSha256),
	@('updater VM executor', $UpdaterVmExecutorPath, $UpdaterVmExecutorSha256),
	@('updater VM receipt', $UpdaterVmReceiptPath, $UpdaterVmReceiptSha256)
)
$resolvedInputs = @{}
foreach ($input in $protectedInputs) {
	$item = Get-Item -LiteralPath $input[1] -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw "Protected $($input[0]) must be a regular file."
	}
	$fullPath = [IO.Path]::GetFullPath($item.FullName)
	if ($fullPath.StartsWith("$sourceRootPath\", [StringComparison]::OrdinalIgnoreCase)) {
		throw "Protected $($input[0]) must live outside the source checkout."
	}
	if ((Get-ReleaseFileSha256 -Path $fullPath) -cne [string]$input[2]) {
		throw "Protected $($input[0]) does not match its pinned SHA-256."
	}
	$resolvedInputs[[string]$input[0]] = $fullPath
}
if ([IO.Path]::GetExtension($resolvedInputs.executor) -cne '.ps1') {
	throw 'Protected rehearsal executor must be a PowerShell script.'
}
$executorSource = Get-Content -LiteralPath $resolvedInputs.executor -Raw
foreach ($forbiddenPattern in @(
	'(?i)\bgh\s+release\b', '(?i)api\.github\.com/.*/releases', '(?i)artifact[- ]?signing',
	'(?i)trusted[- ]?signing', '(?i)\bazure\b', '(?i)contents\s*:\s*write'
)) {
	if ($executorSource -match $forbiddenPattern) {
		throw "Protected rehearsal executor contains forbidden production/release capability pattern '$forbiddenPattern'."
	}
}

$outputRootPath = [IO.Path]::GetFullPath($OutputRoot)
Remove-Item -LiteralPath $outputRootPath -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $outputRootPath | Out-Null
$keyRoot = Join-Path ([IO.Path]::GetTempPath()) ('mumble-rehearsal-ephemeral-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $keyRoot | Out-Null
$pfxPath = Join-Path $keyRoot 'ephemeral-test-signing.pfx'
$ed25519Path = Join-Path $keyRoot 'ephemeral-test-ed25519.pem'
$certificate = $null
$passwordText = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$securePassword = ConvertTo-SecureString -String $passwordText -AsPlainText -Force
$buildId = Get-InputEnhancementBuildId -BuildNumber $BuildNumber -SourceSha $SourceSha
$subject = "CN=Mumble Input Enhancement Rehearsal $BuildNumber-$($SourceSha.Substring(0, 12))"
$executorSucceeded = $false
try {
	$certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
		-CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable `
		-HashAlgorithm SHA256 -NotAfter (Get-Date).AddDays(1)
	Export-PfxCertificate -Cert $certificate -FilePath $pfxPath -Password $securePassword | Out-Null
	& $openssl genpkey -algorithm Ed25519 -out $ed25519Path
	if ($LASTEXITCODE -ne 0) { throw 'OpenSSL failed to generate the ephemeral test Ed25519 key.' }
	$privateKeyBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ed25519Path))
	$publicKeyHex = Get-Ed25519PublicKeyHexFromPrivateKey -PrivateKeyBase64 $privateKeyBase64 -OpenSslPath $openssl
	$env:MUMBLE_REHEARSAL_PFX_PASSWORD = $passwordText
	$executorArguments = @{
		SourceRoot                              = $sourceRootPath
		SourceSha                               = $SourceSha
		BuildNumber                             = $BuildNumber
		UnsignedHandoffArchivePath              = $resolvedInputs['unsigned handoff archive']
		MeasuredEvidenceArchivePath             = $resolvedInputs['measured evidence archive']
		ListeningQualificationPath              = $resolvedInputs['listening qualification']
		ReleaseSmokeHarnessPath                 = $resolvedInputs['release-smoke harness']
		FixtureManifestPath                     = $resolvedInputs['fixture manifest']
		CaseSetPath                             = $resolvedInputs['case set']
		ServerExecutablePath                    = $resolvedInputs['OG server executable']
		KillSwitchObserverPath                  = $resolvedInputs['kill-switch observer']
		KillSwitchObserverSha256                = $KillSwitchObserverSha256
		KillSwitchObserverReceiptPath           = $resolvedInputs['kill-switch observer receipt']
		KillSwitchObserverReceiptSha256         = $KillSwitchObserverReceiptSha256
		UpdaterVmExecutorPath                    = $resolvedInputs['updater VM executor']
		UpdaterVmExecutorSha256                  = $UpdaterVmExecutorSha256
		UpdaterVmReceiptPath                     = $resolvedInputs['updater VM receipt']
		UpdaterVmReceiptSha256                   = $UpdaterVmReceiptSha256
		UpdaterVmImageSha256                     = $UpdaterVmImageSha256
		UpdaterVmSnapshotSha256                  = $UpdaterVmSnapshotSha256
		UpdaterVmHardwareFingerprintSha256       = $UpdaterVmHardwareFingerprintSha256
		EphemeralPfxPath                        = $pfxPath
		EphemeralPfxPasswordEnvironmentVariable = 'MUMBLE_REHEARSAL_PFX_PASSWORD'
		EphemeralCertificateSubject             = $subject
		EphemeralCertificateThumbprint          = $certificate.Thumbprint
		EphemeralEd25519PrivateKeyPath           = $ed25519Path
		EphemeralEd25519PublicKeyHex             = $publicKeyHex
		TimestampUrl                            = $TimestampUrl
		DraftArtifactName                       = $DraftArtifactName
		OutputRoot                              = $outputRootPath
	}
	& $resolvedInputs.executor @executorArguments
	if (-not $?) { throw 'Protected release-rehearsal executor returned failure.' }
	$executorSucceeded = $true
} finally {
	Remove-Item Env:MUMBLE_REHEARSAL_PFX_PASSWORD -ErrorAction SilentlyContinue
	$passwordText = $null
	$securePassword = $null
	if ($certificate) {
		Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $certificate.Thumbprint) -Force -ErrorAction SilentlyContinue
	}
	Remove-Item -LiteralPath $keyRoot -Recurse -Force -ErrorAction SilentlyContinue
}
if (-not $executorSucceeded) { throw 'Release rehearsal did not complete.' }

# The rehearsal executor receives ephemeral signing material for the draft and
# therefore cannot attest its own runtime/VM observations. Replace those files
# with exact protected receipts acquired independently of that executor.
Copy-Item -LiteralPath $resolvedInputs['kill-switch observer receipt'] `
	-Destination (Join-Path $outputRootPath 'kill-switch-observer-receipt.json') -Force
Copy-Item -LiteralPath $resolvedInputs['updater VM receipt'] `
	-Destination (Join-Path $outputRootPath 'updater-vm-receipt.json') -Force

# Qualification schema v3 is inseparable from its hash-listed canonical
# evidence tree.  Preserve that tree beside the executor's flat qualification
# artifact so the final and remotely downloaded draft can recompute every
# listening gate instead of trusting aggregate totals.
$listeningInput = Read-ReleaseJson -Path $resolvedInputs['listening qualification']
$listeningSessionManifest = Assert-ObjectProperty $listeningInput 'session_manifest' 'Listening qualification'
$listeningEvidenceName = Assert-SafeRelativeReleasePath `
	-Path ([string](Assert-ObjectProperty $listeningSessionManifest 'evidence_root' 'Listening session manifest')) `
	-Context 'Listening qualification evidence root'
if ($listeningEvidenceName.Contains('/') -or $listeningEvidenceName -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*\.evidence$') {
	throw 'Listening qualification evidence root must be a direct sibling directory.'
}
$listeningInputParent = Split-Path -Parent $resolvedInputs['listening qualification']
$listeningEvidenceSource = [IO.Path]::GetFullPath((Join-Path $listeningInputParent $listeningEvidenceName))
$listeningEvidenceSourceItem = Get-Item -LiteralPath $listeningEvidenceSource -Force -ErrorAction Stop
if (-not $listeningEvidenceSourceItem.PSIsContainer -or
	($listeningEvidenceSourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
	@(Get-ChildItem -LiteralPath $listeningEvidenceSource -Force -Recurse | Where-Object {
		($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
	}).Count -ne 0) {
	throw 'Listening qualification evidence must be a regular, non-reparse directory tree.'
}
$listeningEvidenceDestination = [IO.Path]::GetFullPath((Join-Path $outputRootPath $listeningEvidenceName))
$outputPrefix = $outputRootPath.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) +
	[IO.Path]::DirectorySeparatorChar
if (-not $listeningEvidenceDestination.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'Listening qualification evidence destination escapes the rehearsal output root.'
}
if (Test-Path -LiteralPath $listeningEvidenceDestination) {
	$destinationItem = Get-Item -LiteralPath $listeningEvidenceDestination -Force
	if (-not $destinationItem.PSIsContainer -or
		($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw 'Executor produced an unsafe listening qualification evidence destination.'
	}
} else {
	Copy-Item -LiteralPath $listeningEvidenceSource -Destination $listeningEvidenceDestination -Recurse
}

$rehearsal = Read-ReleaseJson -Path (Join-Path $outputRootPath 'rehearsal.json')
if ([string]$rehearsal.ephemeralSigning.certificateSubject -cne $subject -or
	[string]$rehearsal.ephemeralSigning.certificateThumbprint -cne [string]$certificate.Thumbprint -or
	[string]$rehearsal.ephemeralSigning.ed25519PublicKeyHex -cne $publicKeyHex) {
	throw 'Rehearsal evidence does not identify the exact ephemeral keys generated by this invocation.'
}

& (Join-Path $PSScriptRoot 'assert-input-enhancement-release-rehearsal.ps1') `
	-Root $outputRootPath -ExpectedSourceSha $SourceSha -ExpectedBuildId $buildId `
	-ExpectedDraftArtifactName $DraftArtifactName `
	-ExpectedExecutorSha256 $ExecutorSha256 `
	-ExpectedUnsignedHandoffSha256 $UnsignedHandoffArchiveSha256 `
	-ExpectedMeasuredEvidenceArchiveSha256 $MeasuredEvidenceArchiveSha256 `
	-ExpectedListeningQualificationSha256 $ListeningQualificationSha256 `
	-ExpectedReleaseSmokeHarnessSha256 $ReleaseSmokeHarnessSha256 `
	-ExpectedFixtureManifestSha256 $FixtureManifestSha256 `
	-ExpectedCaseSetSha256 $CaseSetSha256 `
	-ExpectedServerExecutableSha256 $ServerExecutableSha256 `
	-ExpectedKillSwitchObserverSha256 $KillSwitchObserverSha256 `
	-ExpectedKillSwitchObserverReceiptSha256 $KillSwitchObserverReceiptSha256 `
	-ExpectedUpdaterVmExecutorSha256 $UpdaterVmExecutorSha256 `
	-ExpectedUpdaterVmReceiptSha256 $UpdaterVmReceiptSha256 `
	-ExpectedUpdaterVmImageSha256 $UpdaterVmImageSha256 `
	-ExpectedUpdaterVmSnapshotSha256 $UpdaterVmSnapshotSha256 `
	-ExpectedUpdaterVmHardwareFingerprintSha256 $UpdaterVmHardwareFingerprintSha256 `
	-ExpectedCommunitySize $CommunitySize -OpenSslPath $openssl

& (Join-Path $PSScriptRoot 'new-input-enhancement-rehearsal-draft-manifest.ps1') `
	-Root $outputRootPath -ArtifactName $DraftArtifactName
& (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-draft-manifest.ps1') `
	-Root $outputRootPath -ExpectedArtifactName $DraftArtifactName

Write-Host "Local pre-Azure release rehearsal '$DraftArtifactName' is ready for remote artifact-store re-verification."
