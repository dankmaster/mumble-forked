[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string]$PreparedRoot,
	[Parameter(Mandatory = $true)] [string]$ChallengePath,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{40}$')] [string]$ExpectedSourceSha,
	[Parameter(Mandatory = $true)]
	[ValidatePattern('^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$')]
	[string]$ExpectedBuildId,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedPrepareExecutorSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedUnsignedHandoffSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedMeasuredEvidenceSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedListeningQualificationSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedReleaseSmokeHarnessSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedFixtureManifestSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedCaseSetSha256,
	[Parameter(Mandatory = $true)] [ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedServerExecutableSha256,
	[ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedChallengeId = '',
	[ValidatePattern('^[0-9a-f]{64}$')] [string]$ExpectedCandidateBuildReceiptSha256 = '',
	[switch]$RequireCanonicalJson
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'InputEnhancementReleaseTools.psm1') -Force

function Assert-ExactProperties {
	param([object]$Object, [string[]]$Names, [string]$Context)
	$actual = @($Object.PSObject.Properties.Name | Sort-Object)
	$expected = @($Names | Sort-Object)
	if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
		$missing = @($expected | Where-Object { $_ -cnotin $actual })
		$unexpected = @($actual | Where-Object { $_ -cnotin $expected })
		throw "$Context has missing [$($missing -join ', ')] or unexpected [$($unexpected -join ', ')] properties."
	}
}

function Assert-RegularRoot {
	param([string]$Path, [string]$Context)
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
	if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
		throw "$Context must be a regular, non-reparse directory."
	}
	$resolved = [IO.Path]::GetFullPath($item.FullName).TrimEnd('\', '/')
	if ($resolved.Equals([IO.Path]::GetPathRoot($resolved).TrimEnd('\', '/'),
		[StringComparison]::OrdinalIgnoreCase)) {
		throw "$Context cannot be a filesystem root."
	}
	return $resolved
}

function Resolve-ContainedPath {
	param([string]$Root, [string]$RelativePath, [string]$Context, [switch]$Directory)
	$relative = Assert-SafeRelativeReleasePath -Path $RelativePath -Context $Context
	if ($relative.Contains('\')) { throw "$Context must use canonical forward slashes." }
	$rootPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	$resolved = [IO.Path]::GetFullPath((Join-Path $Root $relative.Replace('/', [IO.Path]::DirectorySeparatorChar)))
	if (-not $resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw "$Context escapes its declared root."
	}
	$item = Get-Item -LiteralPath $resolved -Force -ErrorAction Stop
	if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
		($Directory -and -not $item.PSIsContainer) -or (-not $Directory -and $item.PSIsContainer)) {
		throw "$Context is not the expected regular filesystem object."
	}
	return $item
}

function Get-CanonicalTreeSha256 {
	param([object[]]$Records)
	$payload = [ordered]@{ files = @($Records) } | ConvertTo-Json -Depth 4 -Compress
	[byte[]]$bytes = [Text.UTF8Encoding]::new($false).GetBytes($payload)
	$sha = [Security.Cryptography.SHA256]::Create()
	try {
		return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
	} finally {
		$sha.Dispose()
	}
}

function Assert-Tree {
	param([string]$PreparedRootPath, [object]$Descriptor, [string]$Context)
	$relativeRoot = Assert-SafeRelativeReleasePath -Path ([string]$Descriptor.root) -Context "$Context root"
	if ($relativeRoot.Contains('/') -or $relativeRoot.Contains('\')) {
		throw "$Context root must be a direct child of the prepared root."
	}
	$treeRootItem = Resolve-ContainedPath -Root $PreparedRootPath -RelativePath $relativeRoot `
		-Context "$Context root" -Directory
	$treeRoot = [IO.Path]::GetFullPath($treeRootItem.FullName).TrimEnd('\', '/')
	$unsafe = @(Get-ChildItem -LiteralPath $treeRoot -Force -Recurse | Where-Object {
		($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
	})
	if ($unsafe.Count -ne 0) { throw "$Context contains reparse points." }

	$actual = @(Get-ChildItem -LiteralPath $treeRoot -Force -Recurse -File | ForEach-Object {
		$relative = $_.FullName.Substring($treeRoot.Length).TrimStart('\', '/').Replace('\', '/')
		if ([IO.Path]::GetExtension($relative).ToLowerInvariant() -in @(
			'.key', '.p12', '.pem', '.pfx', '.p8', '.ppk', '.jks', '.kdbx', '.keystore'
		)) { throw "$Context contains forbidden private-material file '$relative'." }
		[ordered]@{ path = $relative; sha256 = Get-ReleaseFileSha256 -Path $_.FullName; size = [int64]$_.Length }
	} | Sort-Object -Property @{ Expression = { $_.path }; Ascending = $true })
	$declared = @($Descriptor.files)
	if ($declared.Count -ne $actual.Count) { throw "$Context file inventory is incomplete." }
	for ($index = 0; $index -lt $declared.Count; ++$index) {
		Assert-ExactProperties $declared[$index] @('path', 'sha256', 'size') "$Context file[$index]"
		$path = Assert-SafeRelativeReleasePath -Path ([string]$declared[$index].path) -Context "$Context file[$index]"
		if ($path.Contains('\') -or $path -cne [string]$actual[$index].path -or
			[string]$declared[$index].sha256 -cne [string]$actual[$index].sha256 -or
			[int64]$declared[$index].size -ne [int64]$actual[$index].size) {
			throw "$Context file inventory differs from the exact filesystem bytes."
		}
	}
	$treeSha = Get-CanonicalTreeSha256 -Records $actual
	if ([string]$Descriptor.treeSha256 -cne $treeSha) { throw "$Context tree hash is invalid." }
	return [ordered]@{ root = $treeRoot; files = $actual; treeSha256 = $treeSha }
}

function Get-RecordMap {
	param([object[]]$Records)
	$map = @{}
	foreach ($record in $Records) {
		if ($map.ContainsKey([string]$record.path)) { throw "Duplicate challenge file '$($record.path)'." }
		$map[[string]$record.path] = $record
	}
	return $map
}

function Assert-LowerSha256 {
	param([object]$Value, [string]$Context)
	if ([string]$Value -cnotmatch '^[0-9a-f]{64}$') { throw "$Context is not a lowercase SHA-256." }
	return [string]$Value
}

$preparedRootPath = Assert-RegularRoot -Path $PreparedRoot -Context 'Prepared rehearsal root'
$challengeItem = Get-Item -LiteralPath $ChallengePath -Force -ErrorAction Stop
if ($challengeItem.PSIsContainer -or ($challengeItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
	throw 'Rehearsal challenge must be a regular file.'
}
$challengePathResolved = [IO.Path]::GetFullPath($challengeItem.FullName)
$preparedPrefix = $preparedRootPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $challengePathResolved.StartsWith($preparedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'Rehearsal challenge must be contained by the prepared root.'
}

$challenge = Read-ReleaseJson -Path $challengePathResolved
Assert-ExactProperties $challenge @(
	'bindings', 'buildId', 'challengeId', 'createdAtUtc', 'ephemeralSigning', 'kind', 'phase', 'schemaVersion',
	'security', 'signed', 'sourceSha', 'transformation', 'unsigned'
) 'Rehearsal challenge'
if ([int]$challenge.schemaVersion -ne 1 -or
	[string]$challenge.kind -cne 'input-enhancement-pre-azure-rehearsal-challenge' -or
	[string]$challenge.phase -cne 'prepared' -or
	[string]$challenge.sourceSha -cne $ExpectedSourceSha -or
	[string]$challenge.buildId -cne $ExpectedBuildId -or
	[string]$challenge.challengeId -cnotmatch '^[0-9a-f]{64}$' -or
	(-not [string]::IsNullOrWhiteSpace($ExpectedChallengeId) -and
		[string]$challenge.challengeId -cne $ExpectedChallengeId)) {
	throw 'Rehearsal challenge identity or phase is invalid.'
}
$createdAt = [datetimeoffset]::MinValue
if (-not [datetimeoffset]::TryParse([string]$challenge.createdAtUtc,
	[Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind,
	[ref]$createdAt)) { throw 'Rehearsal challenge timestamp is invalid.' }

$security = $challenge.security
Assert-ExactProperties $security @(
	'azureUsed', 'contentsWrite', 'draftCreated', 'privateMaterialIncluded', 'productionCredentialsUsed'
) 'Rehearsal challenge security'
if ($security.azureUsed -ne $false -or $security.contentsWrite -ne $false -or
	$security.draftCreated -ne $false -or $security.privateMaterialIncluded -ne $false -or
	$security.productionCredentialsUsed -ne $false) {
	throw 'Prepare phase used publication, production credentials, or retained private material.'
}

$signing = $challenge.ephemeralSigning
Assert-ExactProperties $signing @(
	'certificateSubject', 'certificateThumbprint', 'ed25519PublicKeyHex', 'privateMaterialDeleted', 'testOnly'
) 'Rehearsal challenge signing'
$publicKeyHex = Assert-Ed25519PublicKeyHex -PublicKeyHex ([string]$signing.ed25519PublicKeyHex)
if ($signing.testOnly -ne $true -or $signing.privateMaterialDeleted -ne $true -or
	[string]$signing.certificateSubject -cnotmatch '^CN=Mumble Input Enhancement Rehearsal [A-Za-z0-9._-]+$' -or
	[string]$signing.certificateThumbprint -cnotmatch '^[0-9A-Fa-f]{40}$') {
	throw 'Prepare phase did not attest deletion of ephemeral test signing material.'
}

$bindings = $challenge.bindings
Assert-ExactProperties $bindings @(
	'caseSetSha256', 'fixtureManifestSha256', 'listeningQualificationSha256', 'measuredEvidenceSha256',
	'prepareExecutorSha256', 'releaseSmokeHarnessSha256', 'serverExecutableSha256', 'unsignedHandoffSha256'
) 'Rehearsal challenge bindings'
$expectedBindings = [ordered]@{
	prepareExecutorSha256 = $ExpectedPrepareExecutorSha256
	unsignedHandoffSha256 = $ExpectedUnsignedHandoffSha256
	measuredEvidenceSha256 = $ExpectedMeasuredEvidenceSha256
	listeningQualificationSha256 = $ExpectedListeningQualificationSha256
	releaseSmokeHarnessSha256 = $ExpectedReleaseSmokeHarnessSha256
	fixtureManifestSha256 = $ExpectedFixtureManifestSha256
	caseSetSha256 = $ExpectedCaseSetSha256
	serverExecutableSha256 = $ExpectedServerExecutableSha256
}
foreach ($name in $expectedBindings.Keys) {
	if ([string]$bindings.$name -cne [string]$expectedBindings[$name]) {
		throw "Rehearsal challenge binding '$name' differs from the protected input."
	}
}

$unsignedDescriptor = $challenge.unsigned
$signedDescriptor = $challenge.signed
Assert-ExactProperties $unsignedDescriptor @(
	'candidateBuildReceiptPath', 'candidateBuildReceiptSha256', 'files', 'measuredEvidencePath', 'measuredEvidenceSha256',
	'root', 'stagedPayloadSha256', 'testedBinaryPath', 'testedBinarySha256', 'treeSha256'
) 'Unsigned challenge payload'
Assert-ExactProperties $signedDescriptor @(
	'files', 'installerPath', 'installerSha256', 'policyPath', 'policySha256', 'qualificationPath',
	'qualificationSha256', 'releaseSmokePath', 'releaseSmokeSha256', 'root', 'stagedPayloadSha256',
	'testedBinaryPath', 'testedBinarySha256', 'treeSha256', 'updatePackagePath', 'updatePackageSha256'
) 'Signed challenge payload'
$unsignedTree = Assert-Tree -PreparedRootPath $preparedRootPath -Descriptor $unsignedDescriptor -Context 'Unsigned challenge payload'
$signedTree = Assert-Tree -PreparedRootPath $preparedRootPath -Descriptor $signedDescriptor -Context 'Signed challenge payload'
$null = Assert-LowerSha256 $unsignedDescriptor.stagedPayloadSha256 'Unsigned staged payload hash'
$null = Assert-LowerSha256 $unsignedDescriptor.testedBinarySha256 'Unsigned tested executable hash'
$null = Assert-LowerSha256 $unsignedDescriptor.candidateBuildReceiptSha256 'Candidate build receipt hash'
$null = Assert-LowerSha256 $unsignedDescriptor.measuredEvidenceSha256 'Measured evidence hash'
foreach ($name in @(
	'stagedPayloadSha256', 'testedBinarySha256', 'installerSha256', 'updatePackageSha256',
	'qualificationSha256', 'policySha256', 'releaseSmokeSha256'
)) { $null = Assert-LowerSha256 $signedDescriptor.$name "Signed challenge $name" }
$unsignedMap = Get-RecordMap -Records $unsignedTree.files
$signedMap = Get-RecordMap -Records $signedTree.files

foreach ($entry in @(
	@($unsignedDescriptor, $unsignedMap, 'testedBinaryPath', 'testedBinarySha256', 'Unsigned tested executable'),
	@($unsignedDescriptor, $unsignedMap, 'candidateBuildReceiptPath', 'candidateBuildReceiptSha256', 'Candidate build receipt'),
	@($unsignedDescriptor, $unsignedMap, 'measuredEvidencePath', 'measuredEvidenceSha256', 'Measured evidence'),
	@($signedDescriptor, $signedMap, 'testedBinaryPath', 'testedBinarySha256', 'Signed tested executable'),
	@($signedDescriptor, $signedMap, 'installerPath', 'installerSha256', 'Signed installer'),
	@($signedDescriptor, $signedMap, 'updatePackagePath', 'updatePackageSha256', 'Signed update package'),
	@($signedDescriptor, $signedMap, 'qualificationPath', 'qualificationSha256', 'Signed qualification'),
	@($signedDescriptor, $signedMap, 'policyPath', 'policySha256', 'Signed policy'),
	@($signedDescriptor, $signedMap, 'releaseSmokePath', 'releaseSmokeSha256', 'Signed release smoke')
)) {
	$path = Assert-SafeRelativeReleasePath -Path ([string]$entry[0].($entry[2])) -Context ([string]$entry[4])
	if (-not $entry[1].ContainsKey($path) -or [string]$entry[1][$path].sha256 -cne [string]$entry[0].($entry[3])) {
		throw "$($entry[4]) is not bound to the exact declared tree bytes."
	}
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedCandidateBuildReceiptSha256) -and
	[string]$unsignedDescriptor.candidateBuildReceiptSha256 -cne $ExpectedCandidateBuildReceiptSha256) {
	throw 'Candidate build receipt differs from the expected prepare result.'
}

$transformation = $challenge.transformation
Assert-ExactProperties $transformation @('kind', 'records') 'Rehearsal transformation'
if ([string]$transformation.kind -cne 'authenticode-sign-and-package-v1') {
	throw 'Unsupported rehearsal transformation contract.'
}
$records = @($transformation.records)
$union = @($unsignedMap.Keys + $signedMap.Keys | Sort-Object -Unique)
if ($records.Count -ne $union.Count) { throw 'Transformation does not cover the complete unsigned/signed union.' }
for ($index = 0; $index -lt $records.Count; ++$index) {
	$record = $records[$index]
	Assert-ExactProperties $record @(
		'mode', 'path', 'signedSha256', 'signedSize', 'unsignedSha256', 'unsignedSize'
	) "Transformation record[$index]"
	$path = Assert-SafeRelativeReleasePath -Path ([string]$record.path) -Context "Transformation record[$index]"
	if ($path -cne $union[$index]) { throw 'Transformation records must be complete and path-sorted.' }
	$unsignedRecord = if ($unsignedMap.ContainsKey($path)) { $unsignedMap[$path] } else { $null }
	$signedRecord = if ($signedMap.ContainsKey($path)) { $signedMap[$path] } else { $null }
	switch ([string]$record.mode) {
		'unchanged' {
			if ($null -eq $unsignedRecord -or $null -eq $signedRecord -or
				[string]$unsignedRecord.sha256 -cne [string]$signedRecord.sha256 -or
				[int64]$unsignedRecord.size -ne [int64]$signedRecord.size) {
				throw "Unchanged transformation '$path' changed bytes."
			}
		}
		'authenticode-pe' {
			if ($null -eq $unsignedRecord -or $null -eq $signedRecord -or
				[IO.Path]::GetExtension($path) -cnotin @('.exe', '.dll') -or
				[string]$unsignedRecord.sha256 -ceq [string]$signedRecord.sha256 -or
				[int64]$signedRecord.size -lt [int64]$unsignedRecord.size) {
				throw "Authenticode transformation '$path' is not a declared PE-only mutation."
			}
		}
		'packaged-output' {
			if ($null -ne $unsignedRecord -or $null -eq $signedRecord -or
				[IO.Path]::GetExtension($path) -cnotin @('.json', '.msi', '.sig', '.zip', '.mumble-update')) {
				throw "Packaged output '$path' is not an allowed signed-only artifact."
			}
		}
		default { throw "Unknown transformation mode '$($record.mode)'." }
	}
	if (($null -eq $unsignedRecord -and $null -ne $record.unsignedSha256) -or
		($null -ne $unsignedRecord -and ([string]$record.unsignedSha256 -cne [string]$unsignedRecord.sha256 -or
			[int64]$record.unsignedSize -ne [int64]$unsignedRecord.size)) -or
		($null -eq $signedRecord -and $null -ne $record.signedSha256) -or
		($null -ne $signedRecord -and ([string]$record.signedSha256 -cne [string]$signedRecord.sha256 -or
			[int64]$record.signedSize -ne [int64]$signedRecord.size))) {
		throw "Transformation record '$path' does not bind the exact source/destination bytes."
	}
}

if ($RequireCanonicalJson) {
	$canonical = ($challenge | ConvertTo-Json -Depth 12) + "`n"
	$actual = [IO.File]::ReadAllText($challengePathResolved, [Text.UTF8Encoding]::new($false))
	if ($actual -cne $canonical) { throw 'Rehearsal challenge JSON is not canonical.' }
}

[pscustomobject]@{
	challengeId = [string]$challenge.challengeId
	challengeSha256 = Get-ReleaseFileSha256 -Path $challengePathResolved
	ed25519PublicKeyHex = $publicKeyHex
	unsignedTestedBinarySha256 = [string]$unsignedDescriptor.testedBinarySha256
	unsignedStagedPayloadSha256 = [string]$unsignedDescriptor.stagedPayloadSha256
	signedTestedBinarySha256 = [string]$signedDescriptor.testedBinarySha256
	signedStagedPayloadSha256 = [string]$signedDescriptor.stagedPayloadSha256
	candidateBuildReceiptSha256 = [string]$unsignedDescriptor.candidateBuildReceiptSha256
	installerSha256 = [string]$signedDescriptor.installerSha256
	updatePackageSha256 = [string]$signedDescriptor.updatePackageSha256
}
