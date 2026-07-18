[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$CandidateManifestPath,
	[Parameter(Mandatory = $true)][string]$WindowsCommunityEvidencePath,
	[Parameter(Mandatory = $true)][string]$LinuxMurmurEvidencePath,
	[Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-HexDigest {
	param([AllowNull()][AllowEmptyString()][string]$Value, [int]$Length)
	return -not [string]::IsNullOrWhiteSpace($Value) -and $Value -match "^[0-9a-fA-F]{$Length}$"
}

function Test-IntegerValue {
	param([AllowNull()]$Value)
	return $Value -is [byte] -or $Value -is [sbyte] -or $Value -is [int16] -or
		$Value -is [uint16] -or $Value -is [int32] -or $Value -is [uint32] -or
		$Value -is [int64] -or $Value -is [uint64]
}

function Get-PropertyValue {
	param([AllowNull()]$Object, [Parameter(Mandatory = $true)][string]$Name, $DefaultValue = $null)
	if ($null -eq $Object) { return $DefaultValue }
	$property = $Object.PSObject.Properties[$Name]
	return $property ? $property.Value : $DefaultValue
}

function Get-EvidenceFile {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Label)
	$fullPath = [IO.Path]::GetFullPath($Path)
	if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { throw "$Label does not exist: $fullPath" }
	return [pscustomobject]@{
		path = $fullPath
		sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
	}
}

function Read-JsonEvidence {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Label)
	$file = Get-EvidenceFile -Path $Path -Label $Label
	try {
		$document = Get-Content -LiteralPath $file.path -Raw | ConvertFrom-Json
	} catch {
		throw "$Label is not valid JSON: $($file.path). $($_.Exception.Message)"
	}
	if ($null -eq $document) { throw "$Label is empty: $($file.path)" }
	return [pscustomobject]@{ file = $file; document = $document }
}

function New-TrackResult {
	param(
		[Parameter(Mandatory = $true)][string]$Id,
		[Parameter(Mandatory = $true)][ValidateSet('passed', 'failed')][string]$Status,
		[Parameter(Mandatory = $true)][string]$Reason,
		[string]$EvidencePath = '',
		[string]$EvidenceSha256 = ''
	)
	return [ordered]@{
		id = $Id
		required = $true
		status = $Status
		reason = $Reason
		evidence_path = [string]::IsNullOrWhiteSpace($EvidencePath) ? $null : $EvidencePath
		evidence_sha256 = [string]::IsNullOrWhiteSpace($EvidenceSha256) ? $null : $EvidenceSha256
	}
}

function Write-PromotionEvidence {
	param([Parameter(Mandatory = $true)]$Document)
	$outputFile = [IO.Path]::GetFullPath($OutputPath)
	$outputDirectory = Split-Path -Parent $outputFile
	if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
		New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
	}
	$temporaryPath = "$outputFile.$([Guid]::NewGuid().ToString('N')).tmp"
	try {
		$json = $Document | ConvertTo-Json -Depth 20
		[IO.File]::WriteAllText($temporaryPath, $json + [Environment]::NewLine,
			[Text.UTF8Encoding]::new($false))
		Move-Item -LiteralPath $temporaryPath -Destination $outputFile -Force
	} finally {
		Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
	}
	return $outputFile
}

$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
foreach ($inputPath in @($CandidateManifestPath, $WindowsCommunityEvidencePath, $LinuxMurmurEvidencePath)) {
	if ([string]::Equals($outputFullPath, [IO.Path]::GetFullPath($inputPath), [StringComparison]::OrdinalIgnoreCase)) {
		throw "OutputPath must not overwrite an input evidence file: $outputFullPath"
	}
}

$results = [Collections.Generic.List[object]]::new()
$candidateEvidence = $null
$candidateId = ''
$candidateKind = ''
$sourceRevision = ''
$sourceWorktreeSha256 = ''
$sourceClean = $false
$candidateExecutablePath = ''
$candidateExecutableSha256 = ''

try {
	$candidateEvidence = Read-JsonEvidence -Path $CandidateManifestPath -Label 'Candidate manifest'
	$candidate = $candidateEvidence.document
	$source = Get-PropertyValue -Object $candidate -Name 'source'
	$windows = Get-PropertyValue -Object $candidate -Name 'windows'
	$candidateId = [string](Get-PropertyValue -Object $candidate -Name 'candidate_id' -DefaultValue '')
	$candidateKind = [string](Get-PropertyValue -Object $candidate -Name 'candidate_kind' -DefaultValue '')
	$sourceRevision = ([string](Get-PropertyValue -Object $source -Name 'git_sha' -DefaultValue '')).ToLowerInvariant()
	$sourceWorktreeSha256 = ([string](Get-PropertyValue -Object $source -Name 'worktree_sha256' -DefaultValue '')).ToLowerInvariant()
	$cleanValue = Get-PropertyValue -Object $source -Name 'clean'
	$sourceClean = $cleanValue -is [bool] -and [bool]$cleanValue
	$candidateExecutablePath = [string](Get-PropertyValue -Object $windows -Name 'executable_path' -DefaultValue '')
	$candidateExecutableSha256 = ([string](Get-PropertyValue -Object $windows -Name 'executable_sha256' -DefaultValue '')).ToLowerInvariant()
	if ([int](Get-PropertyValue -Object $candidate -Name 'schema_version' -DefaultValue 0) -ne 1 -or
		$candidateId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{5,127}$' -or
		$candidateKind -cne 'release' -or -not $sourceClean -or
		$sourceRevision -notmatch '^[0-9a-f]{40}$' -or
		-not (Test-HexDigest -Value $sourceWorktreeSha256 -Length 64) -or
		-not (Test-HexDigest -Value $candidateExecutableSha256 -Length 64) -or
		[string]::IsNullOrWhiteSpace($candidateExecutablePath)) {
		throw 'Promotion requires a clean schema-v1 candidate_kind=release manifest.'
	}
	if (-not [IO.Path]::IsPathRooted($candidateExecutablePath)) {
		$candidateExecutablePath = Join-Path (Split-Path -Parent $candidateEvidence.file.path) $candidateExecutablePath
	}
	$candidateExecutablePath = (Resolve-Path -LiteralPath $candidateExecutablePath -ErrorAction Stop).Path
	if ((Get-FileHash -LiteralPath $candidateExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $candidateExecutableSha256) {
		throw 'Candidate executable bytes do not match the candidate manifest.'
	}
	$results.Add((New-TrackResult -Id 'release_candidate' -Status passed `
		-Reason 'Candidate manifest is clean, release-only and bound to the exact Windows executable.' `
		-EvidencePath $candidateEvidence.file.path -EvidenceSha256 $candidateEvidence.file.sha256))
} catch {
	$results.Add((New-TrackResult -Id 'release_candidate' -Status failed -Reason $_.Exception.Message `
		-EvidencePath ([IO.Path]::GetFullPath($CandidateManifestPath)) `
		-EvidenceSha256 $(if ($null -ne $candidateEvidence) { $candidateEvidence.file.sha256 } else { '' })))
}

try {
	$windowsEvidence = Read-JsonEvidence -Path $WindowsCommunityEvidencePath -Label 'Windows community gate evidence'
	$windowsReport = $windowsEvidence.document
	$windowsCandidate = Get-PropertyValue -Object $windowsReport -Name 'candidate'
	$windowsReady = Get-PropertyValue -Object $windowsReport -Name 'ready_for_community_release'
	$windowsGates = @(Get-PropertyValue -Object $windowsReport -Name 'gates' -DefaultValue @())
	if ([int](Get-PropertyValue -Object $windowsReport -Name 'schema_version' -DefaultValue 0) -ne 1 -or
		(Get-PropertyValue -Object $windowsReport -Name 'gate_id' -DefaultValue '') -cne 'windows-qml-community-release-v1') {
		throw 'Windows evidence is not the schema-v1 Windows community release gate.'
	}
	if ($windowsReady -isnot [bool] -or -not [bool]$windowsReady -or
		@(Get-PropertyValue -Object $windowsReport -Name 'failed_required_gates' -DefaultValue @()).Count -ne 0 -or
		@(Get-PropertyValue -Object $windowsReport -Name 'missing_required_gates' -DefaultValue @()).Count -ne 0) {
		throw 'Windows community release gate is not passed and eligible.'
	}
	if ([string](Get-PropertyValue -Object $windowsCandidate -Name 'candidate_id' -DefaultValue '') -cne $candidateId -or
		[string](Get-PropertyValue -Object $windowsCandidate -Name 'candidate_kind' -DefaultValue '') -cne 'release' -or
		[string](Get-PropertyValue -Object $windowsCandidate -Name 'source_commit' -DefaultValue '') -cne $sourceRevision -or
		[string](Get-PropertyValue -Object $windowsCandidate -Name 'source_worktree_sha256' -DefaultValue '') -cne $sourceWorktreeSha256 -or
		(Get-PropertyValue -Object $windowsCandidate -Name 'source_clean') -isnot [bool] -or
		-not [bool](Get-PropertyValue -Object $windowsCandidate -Name 'source_clean') -or
		[string](Get-PropertyValue -Object $windowsCandidate -Name 'executable_sha256' -DefaultValue '') -cne $candidateExecutableSha256) {
		throw 'Windows community evidence does not match the exact clean release candidate.'
	}
	if ($windowsGates.Count -eq 0 -or @($windowsGates | Where-Object {
			(Get-PropertyValue -Object $_ -Name 'required') -isnot [bool] -or
			-not [bool](Get-PropertyValue -Object $_ -Name 'required') -or
			(Get-PropertyValue -Object $_ -Name 'status' -DefaultValue '') -cne 'passed'
		}).Count -ne 0) {
		throw 'Windows community evidence contains a non-passed required subgate.'
	}
	$requiredWindowsGateIds = @(
		'candidate_manifest', 'release_candidate_kind', 'clean_source_tree', 'visual_accessibility',
		'performance', 'connected_product', 'windows_artifacts', 'windows_msi_payload',
		'windows_installer_upgrade'
	)
	$windowsGateIds = @($windowsGates | ForEach-Object { [string](Get-PropertyValue -Object $_ -Name 'id' -DefaultValue '') })
	if (@($windowsGateIds | Group-Object | Where-Object Count -ne 1).Count -ne 0 -or
		@($requiredWindowsGateIds | Where-Object { $windowsGateIds -notcontains $_ }).Count -ne 0) {
		throw 'Windows community evidence is incomplete or contains duplicate required subgate identities.'
	}
	$candidateManifestGates = @($windowsGates | Where-Object { (Get-PropertyValue -Object $_ -Name 'id') -ceq 'candidate_manifest' })
	if ($candidateManifestGates.Count -ne 1 -or $null -eq $candidateEvidence -or
		[string](Get-PropertyValue -Object $candidateManifestGates[0] -Name 'evidence_sha256' -DefaultValue '') -cne $candidateEvidence.file.sha256) {
		throw 'Windows community evidence is not bound to the exact candidate manifest file.'
	}
	$results.Add((New-TrackResult -Id 'windows_client' -Status passed `
		-Reason "Windows community gate and all $($windowsGates.Count) required subgates passed." `
		-EvidencePath $windowsEvidence.file.path -EvidenceSha256 $windowsEvidence.file.sha256))
} catch {
	$results.Add((New-TrackResult -Id 'windows_client' -Status failed -Reason $_.Exception.Message `
		-EvidencePath ([IO.Path]::GetFullPath($WindowsCommunityEvidencePath))))
}

$linuxServerSha256 = ''
$linuxCmakeCacheSha256 = ''
$linuxConfiguration = ''
$linuxBuildNumber = $null
$linuxBuildContract = $null
$linuxTests = $null
try {
	$linuxEvidence = Read-JsonEvidence -Path $LinuxMurmurEvidencePath -Label 'Linux Murmur evidence'
	$linux = $linuxEvidence.document
	$linuxTests = Get-PropertyValue -Object $linux -Name 'tests'
	$linuxServerSha256 = ([string](Get-PropertyValue -Object $linux -Name 'server_sha256' -DefaultValue '')).ToLowerInvariant()
	$linuxCmakeCacheSha256 = ([string](Get-PropertyValue -Object $linux -Name 'cmake_cache_sha256' -DefaultValue '')).ToLowerInvariant()
	$linuxConfiguration = [string](Get-PropertyValue -Object $linux -Name 'configuration' -DefaultValue '')
	$linuxBuildNumber = Get-PropertyValue -Object $linux -Name 'build_number'
	$linuxBuildContract = Get-PropertyValue -Object $linux -Name 'build_contract'
	$testTotal = Get-PropertyValue -Object $linuxTests -Name 'total'
	$testFailures = Get-PropertyValue -Object $linuxTests -Name 'failures'
	$testErrors = Get-PropertyValue -Object $linuxTests -Name 'errors'
	if ([int](Get-PropertyValue -Object $linux -Name 'schema_version' -DefaultValue 0) -ne 1 -or
		[string](Get-PropertyValue -Object $linux -Name 'candidate_git_sha' -DefaultValue '') -cne $sourceRevision -or
		-not (Test-HexDigest -Value $linuxServerSha256 -Length 64) -or
		-not (Test-HexDigest -Value $linuxCmakeCacheSha256 -Length 64) -or
		$linuxConfiguration -cne 'linux-x86_64-static-release-tests' -or
		-not (Test-IntegerValue -Value $linuxBuildNumber) -or [int64]$linuxBuildNumber -lt 0 -or
		(Get-PropertyValue -Object $linux -Name 'client') -isnot [bool] -or [bool](Get-PropertyValue -Object $linux -Name 'client') -or
		(Get-PropertyValue -Object $linux -Name 'server') -isnot [bool] -or -not [bool](Get-PropertyValue -Object $linux -Name 'server')) {
		throw 'Linux evidence is not the required static Murmur test artifact for the candidate source revision.'
	}
	if ((Get-PropertyValue -Object $linuxBuildContract -Name 'build_type' -DefaultValue '') -cne 'Release' -or
		(Get-PropertyValue -Object $linuxBuildContract -Name 'client') -isnot [bool] -or [bool](Get-PropertyValue -Object $linuxBuildContract -Name 'client') -or
		(Get-PropertyValue -Object $linuxBuildContract -Name 'server') -isnot [bool] -or -not [bool](Get-PropertyValue -Object $linuxBuildContract -Name 'server') -or
		(Get-PropertyValue -Object $linuxBuildContract -Name 'screen_helper') -isnot [bool] -or [bool](Get-PropertyValue -Object $linuxBuildContract -Name 'screen_helper') -or
		(Get-PropertyValue -Object $linuxBuildContract -Name 'static') -isnot [bool] -or -not [bool](Get-PropertyValue -Object $linuxBuildContract -Name 'static') -or
		(Get-PropertyValue -Object $linuxBuildContract -Name 'tests') -isnot [bool] -or -not [bool](Get-PropertyValue -Object $linuxBuildContract -Name 'tests')) {
		throw 'Linux evidence does not prove the single-binary server-only, static, tested Release build contract.'
	}
	if ((Get-PropertyValue -Object $linuxTests -Name 'status' -DefaultValue '') -cne 'passed' -or
		-not (Test-IntegerValue -Value $testTotal) -or [int64]$testTotal -le 0 -or
		-not (Test-IntegerValue -Value $testFailures) -or [int64]$testFailures -ne 0 -or
		-not (Test-IntegerValue -Value $testErrors) -or [int64]$testErrors -ne 0 -or
		[string]::IsNullOrWhiteSpace([string](Get-PropertyValue -Object $linuxTests -Name 'result_file' -DefaultValue '')) -or
		-not (Test-HexDigest -Value ([string](Get-PropertyValue -Object $linuxTests -Name 'result_sha256' -DefaultValue '')) -Length 64)) {
		throw 'Linux Murmur promotion requires completed, passed CTest evidence; tests-not-run is candidate-only.'
	}
	$results.Add((New-TrackResult -Id 'linux_murmur' -Status passed `
		-Reason "The exact static Murmur binary and $testTotal CTest results passed from one contracted build tree." `
		-EvidencePath $linuxEvidence.file.path -EvidenceSha256 $linuxEvidence.file.sha256))
} catch {
	$results.Add((New-TrackResult -Id 'linux_murmur' -Status failed -Reason $_.Exception.Message `
		-EvidencePath ([IO.Path]::GetFullPath($LinuxMurmurEvidencePath))))
}

$failedTracks = @($results | Where-Object { $_.required -and $_.status -ne 'passed' } | ForEach-Object { $_.id })
$eligible = $failedTracks.Count -eq 0
$promotion = [ordered]@{
	schema_version = 1
	artifact_kind = 'community_release_promotion_evidence'
	gate_id = 'windows-qml-linux-murmur-community-release'
	status = $eligible ? 'passed' : 'failed'
	promotion_eligible = $eligible
	evaluated_at_utc = [DateTime]::UtcNow.ToString('o')
	promotion_id = [string]::IsNullOrWhiteSpace($candidateId) ? $null : $candidateId
	candidate = [ordered]@{
		id = [string]::IsNullOrWhiteSpace($candidateId) ? $null : $candidateId
		kind = [string]::IsNullOrWhiteSpace($candidateKind) ? $null : $candidateKind
		source_revision = [string]::IsNullOrWhiteSpace($sourceRevision) ? $null : $sourceRevision
		source_worktree_sha256 = [string]::IsNullOrWhiteSpace($sourceWorktreeSha256) ? $null : $sourceWorktreeSha256
		source_clean = $sourceClean
		windows_executable_sha256 = [string]::IsNullOrWhiteSpace($candidateExecutableSha256) ? $null : $candidateExecutableSha256
		candidate_manifest_sha256 = $null -eq $candidateEvidence ? $null : $candidateEvidence.file.sha256
	}
	tracks = @($results)
	linux_murmur = [ordered]@{
		configuration = [string]::IsNullOrWhiteSpace($linuxConfiguration) ? $null : $linuxConfiguration
		build_number = $linuxBuildNumber
		server_sha256 = [string]::IsNullOrWhiteSpace($linuxServerSha256) ? $null : $linuxServerSha256
		cmake_cache_sha256 = [string]::IsNullOrWhiteSpace($linuxCmakeCacheSha256) ? $null : $linuxCmakeCacheSha256
		test_status = $null -eq $linuxTests ? $null : (Get-PropertyValue -Object $linuxTests -Name 'status')
		test_total = $null -eq $linuxTests ? $null : (Get-PropertyValue -Object $linuxTests -Name 'total')
	}
	failed_required_tracks = $failedTracks
}
$writtenPath = Write-PromotionEvidence -Document $promotion
$promotion | ConvertTo-Json -Depth 20
if (-not $eligible) {
	throw "Community release readiness failed closed (failed: $($failedTracks -join ', ')). Evidence: $writtenPath"
}
