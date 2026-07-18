[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$SourceRoot,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$SourceSha,
	[Parameter(Mandatory = $true)] [ValidateRange(1, 2147483647)] [int]$BuildNumber,
	[Parameter(Mandatory = $true)] [string]$PrepareExecutorPath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$PrepareExecutorSha256,
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
	[Parameter(Mandatory = $true)] [string]$AllowedOutputParent,
	[Parameter(Mandatory = $true)] [string]$OutputRoot,
	[string]$OpenSslPath = '',
	[string]$PythonPath = 'python',
	[ValidatePattern('^https://[^\s]+$')] [string]$TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'Release rehearsal prepare is Windows-only.' }
Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force
$openssl = Resolve-InputEnhancementOpenSsl -OpenSslPath $OpenSslPath
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\', '/')
if ((git -C $sourceRootPath rev-parse HEAD).Trim() -cne $SourceSha) {
	throw 'Release rehearsal source checkout is not the requested immutable commit.'
}
if (@(git -C $sourceRootPath status --porcelain=v1 --untracked-files=all).Count -ne 0) {
	throw 'Release rehearsal prepare requires a completely clean source checkout.'
}

$forbiddenEnvironment = @(Get-ChildItem Env: | Where-Object {
	$_.Name -match '^(AZURE_|ACTIONS_ID_TOKEN_REQUEST_|INPUT_ENHANCEMENT_ED25519_PRIVATE_KEY|AZURE_ARTIFACT_SIGNING|TRUSTED_SIGNING)' -or
	$_.Name -match '(?i)(PRODUCTION.*PRIVATE|PROD.*SIGNING.*KEY)'
})
if ($forbiddenEnvironment.Count -ne 0) {
	throw "Pre-Azure prepare refuses production/Azure/OIDC material: $($forbiddenEnvironment.Name -join ', ')."
}

function Resolve-ProtectedFile {
	param([string]$Label, [string]$Path, [string]$Sha256)
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw "Protected $Label must be a regular file."
	}
	$resolved = [IO.Path]::GetFullPath($item.FullName)
	if ($resolved.StartsWith(($sourceRootPath + '\'), [StringComparison]::OrdinalIgnoreCase)) {
		throw "Protected $Label must live outside the source checkout."
	}
	if ((Get-ReleaseFileSha256 -Path $resolved) -cne $Sha256) {
		throw "Protected $Label does not match its pinned SHA-256."
	}
	return $resolved
}

$inputs = [ordered]@{
	prepareExecutor = Resolve-ProtectedFile 'prepare executor' $PrepareExecutorPath $PrepareExecutorSha256
	unsignedHandoff = Resolve-ProtectedFile 'unsigned handoff archive' $UnsignedHandoffArchivePath $UnsignedHandoffArchiveSha256
	measuredEvidence = Resolve-ProtectedFile 'measured evidence archive' $MeasuredEvidenceArchivePath $MeasuredEvidenceArchiveSha256
	listening = Resolve-ProtectedFile 'listening qualification' $ListeningQualificationPath $ListeningQualificationSha256
	releaseSmokeHarness = Resolve-ProtectedFile 'release-smoke harness' $ReleaseSmokeHarnessPath $ReleaseSmokeHarnessSha256
	fixtureManifest = Resolve-ProtectedFile 'release-smoke fixture manifest' $FixtureManifestPath $FixtureManifestSha256
	caseSet = Resolve-ProtectedFile 'release-smoke case set' $CaseSetPath $CaseSetSha256
	server = Resolve-ProtectedFile 'OG server executable' $ServerExecutablePath $ServerExecutableSha256
}
if ([IO.Path]::GetExtension($inputs.prepareExecutor) -cne '.ps1') {
	throw 'Protected prepare executor must be a PowerShell script.'
}
$executorSource = Get-Content -LiteralPath $inputs.prepareExecutor -Raw
foreach ($forbiddenPattern in @(
	'(?i)\bgh\s+release\b', '(?i)api\.github\.com/.*/releases', '(?i)artifact[- ]?signing',
	'(?i)trusted[- ]?signing', '(?i)\bazure\b', '(?i)contents\s*:\s*write'
)) {
	if ($executorSource -match $forbiddenPattern) {
		throw "Protected prepare executor contains forbidden capability '$forbiddenPattern'."
	}
}

$outputRootPath = Initialize-InputEnhancementRehearsalOutputRoot `
	-OutputRoot $OutputRoot -AllowedOutputParent $AllowedOutputParent -SourceRoot $sourceRootPath
$challengeId = ([BitConverter]::ToString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))).Replace('-', '').ToLowerInvariant()
$buildId = Get-InputEnhancementBuildId -BuildNumber $BuildNumber -SourceSha $SourceSha
$privateRoot = Join-Path ([IO.Path]::GetTempPath()) ('mumble-rehearsal-private-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $privateRoot -ErrorAction Stop | Out-Null
$pfxPath = Join-Path $privateRoot 'ephemeral-authenticode.pfx'
$ed25519PrivatePath = Join-Path $privateRoot 'ephemeral-ed25519.pem'
$certificate = $null
$baseCertificate = $null
$rsa = $null
$pfxBytes = $null
$certificateThumbprint = ''
$passwordText = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$securePassword = ConvertTo-SecureString -String $passwordText -AsPlainText -Force
$subject = "CN=Mumble Input Enhancement Rehearsal $BuildNumber-$($SourceSha.Substring(0, 12))"
$publicKeyHex = ''
$executorSucceeded = $false
try {
	$keyOutput = @(& $openssl genpkey -algorithm Ed25519 -out $ed25519PrivatePath 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "Generating the ephemeral rehearsal Ed25519 key failed:`n$($keyOutput -join [Environment]::NewLine)"
	}
	$publicKeyHex = Get-Ed25519PublicKeyHexFromPrivateKey `
		-PrivateKeyBase64 ([Convert]::ToBase64String([IO.File]::ReadAllBytes($ed25519PrivatePath))) `
		-OpenSslPath $openssl
	# Build an in-memory certificate whose private key is imported with
	# EphemeralKeySet. It never enters a persistent Windows certificate store.
	$rsa = [Security.Cryptography.RSA]::Create(3072)
	$request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
		[Security.Cryptography.X509Certificates.X500DistinguishedName]::new($subject), $rsa,
		[Security.Cryptography.HashAlgorithmName]::SHA256,
		[Security.Cryptography.RSASignaturePadding]::Pkcs1)
	$usages = [Security.Cryptography.OidCollection]::new()
	$null = $usages.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
	$null = $request.CertificateExtensions.Add(
		[Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usages, $false))
	$baseCertificate = $request.CreateSelfSigned([datetimeoffset]::UtcNow.AddMinutes(-5), [datetimeoffset]::UtcNow.AddDays(1))
	$pfxBytes = $baseCertificate.Export(
		[Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $passwordText)
	[IO.File]::WriteAllBytes($pfxPath, $pfxBytes)
	$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
		$pfxBytes, $passwordText,
		[Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet -bor
		[Security.Cryptography.X509Certificates.X509KeyStorageFlags]::Exportable)
	$certificateThumbprint = $certificate.Thumbprint
	$env:MUMBLE_REHEARSAL_PFX_PASSWORD = $passwordText
	& $inputs.prepareExecutor `
		-Operation Prepare `
		-SourceRoot $sourceRootPath -SourceSha $SourceSha -BuildNumber $BuildNumber `
		-ChallengeId $challengeId `
		-UnsignedHandoffArchivePath $inputs.unsignedHandoff `
		-MeasuredEvidenceArchivePath $inputs.measuredEvidence `
		-ListeningQualificationPath $inputs.listening `
		-ReleaseSmokeHarnessPath $inputs.releaseSmokeHarness `
		-FixtureManifestPath $inputs.fixtureManifest -CaseSetPath $inputs.caseSet `
		-ServerExecutablePath $inputs.server `
		-EphemeralPfxPath $pfxPath `
		-EphemeralPfxPasswordEnvironmentVariable MUMBLE_REHEARSAL_PFX_PASSWORD `
		-EphemeralCertificateSubject $subject `
		-EphemeralCertificateThumbprint $certificateThumbprint `
		-EphemeralEd25519PrivateKeyPath $ed25519PrivatePath `
		-EphemeralEd25519PublicKeyHex $publicKeyHex `
		-TimestampUrl $TimestampUrl -OutputRoot $outputRootPath
	if (-not $?) { throw 'Protected prepare executor returned failure.' }

	$buildResultPath = Join-Path $outputRootPath 'prepare-build.json'
	$buildResult = Read-ReleaseJson -Path $buildResultPath
	$actualProperties = @($buildResult.PSObject.Properties.Name | Sort-Object)
	$expectedProperties = @(
		'buildRoot', 'candidateBuildReceiptPath', 'candidateBuildReceiptSha256', 'challengeId', 'kind',
		'schemaVersion', 'stageRoot', 'stagedPayloadSha256', 'testedBinarySha256'
	) | Sort-Object
	if (@(Compare-Object $expectedProperties $actualProperties).Count -ne 0 -or
		[int]$buildResult.schemaVersion -ne 1 -or
		[string]$buildResult.kind -cne 'input-enhancement-rehearsal-prepare-build-result' -or
		[string]$buildResult.challengeId -cne $challengeId) {
		throw 'Prepare build result schema or challenge identity is invalid.'
	}
	$rootPrefix = $outputRootPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	$buildRoot = [IO.Path]::GetFullPath([string]$buildResult.buildRoot)
	$stageRoot = [IO.Path]::GetFullPath([string]$buildResult.stageRoot)
	foreach ($path in @($buildRoot, $stageRoot)) {
		$item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
		if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
			-not $path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
			throw 'Prepare build/stage root must be a regular directory contained by the prepared root.'
		}
	}
	$receiptRelative = Assert-SafeRelativeReleasePath `
		-Path ([string]$buildResult.candidateBuildReceiptPath) -Context 'Candidate build receipt'
	$receiptPath = [IO.Path]::GetFullPath((Join-Path $outputRootPath $receiptRelative.Replace('/', '\')))
	if (-not $receiptPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
		(Get-ReleaseFileSha256 -Path $receiptPath) -cne [string]$buildResult.candidateBuildReceiptSha256) {
		throw 'Candidate build receipt is outside the prepared root or differs from prepare-build.json.'
	}
	$receiptOutput = @(& $PythonPath (Join-Path $PSScriptRoot '..\audio-quality\candidate_build_receipt.py') `
		--validate --receipt $receiptPath --source-root $sourceRootPath --expected-commit $SourceSha `
		--build-root $buildRoot --stage-root $stageRoot --public-key-hex $publicKeyHex `
		--expected-executable-sha256 ([string]$buildResult.testedBinarySha256) `
		--expected-stage-payload-sha256 ([string]$buildResult.stagedPayloadSha256) 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "Candidate build receipt failed live validation before signing:`n$($receiptOutput -join [Environment]::NewLine)"
	}
	$executorSucceeded = $true
} finally {
	if (Test-Path Env:MUMBLE_REHEARSAL_PFX_PASSWORD) {
		Remove-Item Env:MUMBLE_REHEARSAL_PFX_PASSWORD -ErrorAction Stop
	}
	$passwordText = $null
	$securePassword = $null
	if ($certificate) { $certificate.Dispose(); $certificate = $null }
	if ($baseCertificate) { $baseCertificate.Dispose(); $baseCertificate = $null }
	if ($rsa) { $rsa.Dispose(); $rsa = $null }
	if ($pfxBytes) { [Array]::Clear($pfxBytes, 0, $pfxBytes.Length); $pfxBytes = $null }
	foreach ($privatePath in @($pfxPath, $ed25519PrivatePath)) {
		if (Test-Path -LiteralPath $privatePath) {
			Remove-Item -LiteralPath $privatePath -Force -ErrorAction Stop
		}
	}
	if (Test-Path -LiteralPath $privateRoot) {
		Remove-Item -LiteralPath $privateRoot -Recurse -Force -ErrorAction Stop
	}
	if ((Test-Path Env:MUMBLE_REHEARSAL_PFX_PASSWORD) -or
		(Test-Path -LiteralPath $privateRoot) -or (Test-Path -LiteralPath $pfxPath) -or
		(Test-Path -LiteralPath $ed25519PrivatePath) -or
		(-not [string]::IsNullOrWhiteSpace($certificateThumbprint) -and
			(Test-Path -LiteralPath ("Cert:\CurrentUser\My\" + $certificateThumbprint)))) {
		throw 'Ephemeral rehearsal signing material cleanup could not be proven.'
	}
}
if (-not $executorSucceeded) { throw 'Release rehearsal prepare did not complete.' }

$challengePath = Join-Path $outputRootPath 'rehearsal-challenge.json'
$challenge = Read-ReleaseJson -Path $challengePath
if ([string]$challenge.challengeId -cne $challengeId -or
	[string]$challenge.ephemeralSigning.ed25519PublicKeyHex -cne $publicKeyHex -or
	[string]$challenge.ephemeralSigning.certificateSubject -cne $subject -or
	[string]$challenge.ephemeralSigning.certificateThumbprint -cne $certificateThumbprint) {
	throw 'Prepared challenge does not identify the exact ephemeral keys and challenge nonce.'
}
$challenge.ephemeralSigning.privateMaterialDeleted = $true
$cleanupVerification = [ordered]@{
	certificateStoreAbsent = $true
	ed25519PrivateKeyAbsent = $true
	passwordEnvironmentAbsent = $true
	pfxAbsent = $true
	privateRootAbsent = $true
}
if ($null -eq $challenge.ephemeralSigning.PSObject.Properties['cleanupVerification']) {
	$challenge.ephemeralSigning | Add-Member -NotePropertyName cleanupVerification -NotePropertyValue $cleanupVerification
} else {
	$challenge.ephemeralSigning.cleanupVerification = $cleanupVerification
}
$challenge.security.privateMaterialIncluded = $false
Write-ReleaseJson -Path $challengePath -Value $challenge
Remove-Item -LiteralPath (Join-Path $outputRootPath 'prepare-build.json') -Force
if ((Test-Path -LiteralPath (Join-Path $outputRootPath 'draft-manifest.json')) -or
	(Test-Path -LiteralPath (Join-Path $outputRootPath 'rehearsal.json'))) {
	throw 'Prepare phase must exit before draft/final rehearsal creation.'
}
$allowedTopLevel = @('rehearsal-challenge.json', 'signed', 'unsigned')
$unexpectedTopLevel = @(Get-ChildItem -LiteralPath $outputRootPath -Force | Where-Object {
	$_.Name -cnotin $allowedTopLevel
})
if ($unexpectedTopLevel.Count -ne 0) {
	throw "Prepare phase produced unbound top-level entries: $($unexpectedTopLevel.Name -join ', ')."
}
foreach ($item in @(Get-ChildItem -LiteralPath $outputRootPath -Force -Recurse)) {
	if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw "Prepare phase contains a reparse point: '$($item.FullName)'."
	}
	if (-not $item.PSIsContainer) {
		$relative = [IO.Path]::GetRelativePath($outputRootPath, $item.FullName).Replace('\', '/')
		Assert-InputEnhancementRehearsalDraftFileSafety -File $item -RelativePath $relative
	}
}

$challengeResult = & (Join-Path $PSScriptRoot 'assert-input-enhancement-rehearsal-challenge.ps1') `
	-PreparedRoot $outputRootPath -ChallengePath $challengePath `
	-ExpectedSourceSha $SourceSha -ExpectedBuildId $buildId `
	-ExpectedPrepareExecutorSha256 $PrepareExecutorSha256 `
	-ExpectedUnsignedHandoffSha256 $UnsignedHandoffArchiveSha256 `
	-ExpectedMeasuredEvidenceSha256 $MeasuredEvidenceArchiveSha256 `
	-ExpectedListeningQualificationSha256 $ListeningQualificationSha256 `
	-ExpectedReleaseSmokeHarnessSha256 $ReleaseSmokeHarnessSha256 `
	-ExpectedFixtureManifestSha256 $FixtureManifestSha256 `
	-ExpectedCaseSetSha256 $CaseSetSha256 `
	-ExpectedServerExecutableSha256 $ServerExecutableSha256 `
	-ExpectedChallengeId $challengeId `
	-ExpectedCandidateBuildReceiptSha256 ([string]$buildResult.candidateBuildReceiptSha256) `
	-RequireCanonicalJson
if ([string]$challengeResult.unsignedTestedBinarySha256 -cne [string]$buildResult.testedBinarySha256 -or
	[string]$challengeResult.unsignedStagedPayloadSha256 -cne [string]$buildResult.stagedPayloadSha256) {
	throw 'Prepared challenge unsigned identity differs from the independently validated candidate-build receipt.'
}

Write-Host "Prepared fail-closed rehearsal challenge '$challengeId' for '$buildId'; no draft was created."
$challengeResult
