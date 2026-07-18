[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$CandidateManifestPath,
	[Parameter(Mandatory = $true)][string]$VisualEvidencePath,
	[Parameter(Mandatory = $true)][string]$PerformanceEvidencePath,
	[Parameter(Mandatory = $true)][string]$ConnectedEvidencePath,
	[Parameter(Mandatory = $true)][string]$WindowsArtifactEvidencePath,
	[string]$MsiPayloadEvidencePath = "",
	[Parameter(Mandatory = $true)][string]$InstallerUpgradeEvidencePath,
	[string]$OutputPath = ".tmp\windows-community-release-gate.json"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module "$PSScriptRoot\WindowsMsiPayload.Common.psm1" -Force

function Test-HexDigest {
	param([AllowNull()][AllowEmptyString()][string]$Value, [int]$Length)
	return -not [string]::IsNullOrWhiteSpace($Value) -and $Value -match "^[0-9a-fA-F]{$Length}$"
}

function Get-EvidenceFile {
	param([Parameter(Mandatory = $true)][string]$Path)
	$fullPath = [IO.Path]::GetFullPath($Path)
	if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
		throw "Evidence file does not exist: $fullPath"
	}
	return [pscustomobject]@{
		path = $fullPath
		sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
	}
}

function Read-JsonEvidence {
	param([Parameter(Mandatory = $true)][string]$Path)
	$file = Get-EvidenceFile -Path $Path
	try {
		$document = Get-Content -LiteralPath $file.path -Raw | ConvertFrom-Json
	} catch {
		throw "Evidence file is not valid JSON: $($file.path). $($_.Exception.Message)"
	}
	if ($null -eq $document) { throw "Evidence JSON is empty: $($file.path)" }
	return [pscustomobject]@{ file = $file; document = $document }
}

function Get-PropertyValue {
	param($Object, [Parameter(Mandatory = $true)][string]$Name, $Default = $null)
	if ($null -eq $Object) { return $Default }
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property) { return $Default }
	return $property.Value
}

function New-GateResult {
	param(
		[Parameter(Mandatory = $true)][string]$Id,
		[Parameter(Mandatory = $true)][ValidateSet("passed", "failed", "missing")][string]$Status,
		[Parameter(Mandatory = $true)][string]$Reason,
		[string]$EvidencePath = "",
		[string]$EvidenceSha256 = ""
	)
	return [ordered]@{
		id = $Id
		required = $true
		status = $Status
		reason = $Reason
		evidence_path = if ([string]::IsNullOrWhiteSpace($EvidencePath)) { $null } else { $EvidencePath }
		evidence_sha256 = if ([string]::IsNullOrWhiteSpace($EvidenceSha256)) { $null } else { $EvidenceSha256 }
	}
}

function Test-AllBooleanGatesPassed {
	param($GateObject)
	if ($null -eq $GateObject) { return $false }
	$properties = @($GateObject.PSObject.Properties)
	if ($properties.Count -eq 0) { return $false }
	foreach ($property in $properties) {
		if ($property.Value -isnot [bool] -or -not [bool]$property.Value) { return $false }
	}
	return $true
}

$gateResults = [Collections.Generic.List[object]]::new()
$candidate = $null
$candidateEvidence = $null
$candidateId = ""
$candidateKind = ""
$sourceCommit = ""
$sourceWorktreeSha256 = ""
$sourceClean = $false
$candidateExecutablePath = ""
$candidateExecutableSha256 = ""
$resolvedArtifactPaths = @()
$candidateArtifactMsiPath = ""
$candidateArtifactMsiSha256 = ""
$msiPayloadEvidence = $null
$msiPayloadEvidenceFile = $null

try {
	$candidateEvidence = Read-JsonEvidence -Path $CandidateManifestPath
	$candidate = $candidateEvidence.document
	$candidateId = [string](Get-PropertyValue -Object $candidate -Name "candidate_id" -Default "")
	$candidateKind = [string](Get-PropertyValue -Object $candidate -Name "candidate_kind" -Default "")
	$source = Get-PropertyValue -Object $candidate -Name "source"
	$windows = Get-PropertyValue -Object $candidate -Name "windows"
	$sourceCommit = ([string](Get-PropertyValue -Object $source -Name "git_sha" -Default "")).ToLowerInvariant()
	$sourceWorktreeSha256 = ([string](Get-PropertyValue -Object $source -Name "worktree_sha256" -Default "")).ToLowerInvariant()
	$cleanValue = Get-PropertyValue -Object $source -Name "clean"
	$sourceClean = $cleanValue -is [bool] -and [bool]$cleanValue
	$candidateExecutablePath = [string](Get-PropertyValue -Object $windows -Name "executable_path" -Default "")
	$candidateExecutableSha256 = ([string](Get-PropertyValue -Object $windows -Name "executable_sha256" -Default "")).ToLowerInvariant()

	$manifestValid = (Get-PropertyValue -Object $candidate -Name "schema_version" -Default 0) -eq 1 -and
		$candidateId -match '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$' -and
		$candidateKind -in @("development", "release") -and
		$sourceCommit -match '^[0-9a-f]{40,64}$' -and
		(Test-HexDigest -Value $sourceWorktreeSha256 -Length 64) -and
		-not [string]::IsNullOrWhiteSpace($candidateExecutablePath) -and
		(Test-HexDigest -Value $candidateExecutableSha256 -Length 64)
	if (-not $manifestValid) { throw "Candidate manifest does not satisfy schema v1." }

	$resolvedExecutable = (Resolve-Path -LiteralPath $candidateExecutablePath).Path
	$actualExecutableSha256 = (Get-FileHash -LiteralPath $resolvedExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($actualExecutableSha256 -cne $candidateExecutableSha256) {
		throw "Candidate executable SHA-256 does not match candidate manifest."
	}
	$candidateExecutablePath = $resolvedExecutable
	$gateResults.Add((New-GateResult -Id "candidate_manifest" -Status passed -Reason "Candidate manifest schema and executable hash are valid." -EvidencePath $candidateEvidence.file.path -EvidenceSha256 $candidateEvidence.file.sha256))
} catch {
	$evidencePath = if ($null -ne $candidateEvidence) { $candidateEvidence.file.path } else { [IO.Path]::GetFullPath($CandidateManifestPath) }
	$evidenceHash = if ($null -ne $candidateEvidence) { $candidateEvidence.file.sha256 } else { "" }
	$gateResults.Add((New-GateResult -Id "candidate_manifest" -Status failed -Reason $_.Exception.Message -EvidencePath $evidencePath -EvidenceSha256 $evidenceHash))
}

$gateResults.Add((New-GateResult -Id "release_candidate_kind" `
	-Status $(if ($candidateKind -ceq "release") { "passed" } else { "failed" }) `
	-Reason $(if ($candidateKind -ceq "release") { "Candidate is marked for release." } else { "Final release readiness requires candidate_kind=release." }) `
	-EvidencePath $(if ($null -ne $candidateEvidence) { $candidateEvidence.file.path } else { "" }) `
	-EvidenceSha256 $(if ($null -ne $candidateEvidence) { $candidateEvidence.file.sha256 } else { "" })))
$gateResults.Add((New-GateResult -Id "clean_source_tree" `
	-Status $(if ($sourceClean) { "passed" } else { "failed" }) `
	-Reason $(if ($sourceClean) { "Candidate source is recorded as clean." } else { "Final release readiness requires source.clean=true." }) `
	-EvidencePath $(if ($null -ne $candidateEvidence) { $candidateEvidence.file.path } else { "" }) `
	-EvidenceSha256 $(if ($null -ne $candidateEvidence) { $candidateEvidence.file.sha256 } else { "" })))

try {
	$visualEvidence = Read-JsonEvidence -Path $VisualEvidencePath
	$visual = $visualEvidence.document
	$visualCases = @(Get-PropertyValue -Object $visual -Name "cases" -Default @())
	if ((Get-PropertyValue -Object $visual -Name "schema_version" -Default 0) -ne 1 -or
		(Get-PropertyValue -Object $visual -Name "frontend" -Default "") -cne "qml" -or
		(Get-PropertyValue -Object $visual -Name "mode" -Default "") -cne "gate") {
		throw "Visual evidence must be a schema-v1 QML baseline gate; candidate-only evidence is not releasable."
	}
	if ([string](Get-PropertyValue -Object $visual -Name "source_git_sha" -Default "") -cne $sourceCommit) {
		throw "Visual evidence source_git_sha does not match the candidate source commit."
	}
	if (-not (Test-HexDigest -Value ([string](Get-PropertyValue -Object $visual -Name "executable_sha256" -Default "")) -Length 64)) {
		throw "Visual evidence has no valid executable SHA-256."
	}
	if ($visualCases.Count -eq 0) { throw "Visual evidence contains no cases." }
	$caseIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($visualCase in $visualCases) {
		$caseId = [string](Get-PropertyValue -Object $visualCase -Name "id" -Default "")
		if ([string]::IsNullOrWhiteSpace($caseId) -or -not $caseIds.Add($caseId) -or
			-not (Test-HexDigest -Value ([string](Get-PropertyValue -Object $visualCase -Name "image_sha256" -Default "")) -Length 64) -or
			-not (Test-HexDigest -Value ([string](Get-PropertyValue -Object $visualCase -Name "accessibility_sha256" -Default "")) -Length 64)) {
			throw "Visual evidence contains an invalid or duplicate case."
		}
	}
	$gateResults.Add((New-GateResult -Id "visual_accessibility" -Status passed -Reason "Validated $($visualCases.Count) baseline-compared visual/accessibility cases." -EvidencePath $visualEvidence.file.path -EvidenceSha256 $visualEvidence.file.sha256))
} catch {
	$gateResults.Add((New-GateResult -Id "visual_accessibility" -Status failed -Reason $_.Exception.Message -EvidencePath ([IO.Path]::GetFullPath($VisualEvidencePath))))
}

try {
	$performanceEvidence = Read-JsonEvidence -Path $PerformanceEvidencePath
	$performance = $performanceEvidence.document
	if ((Get-PropertyValue -Object $performance -Name "schema_version" -Default 0) -ne 2 -or
		(Get-PropertyValue -Object $performance -Name "contract_id" -Default "") -cne "windows-qml-performance-v2") {
		throw "Performance evidence does not satisfy windows-qml-performance-v2."
	}
	if ([string](Get-PropertyValue -Object $performance -Name "candidate_id" -Default "") -cne $candidateId -or
		[string](Get-PropertyValue -Object $performance -Name "source_commit" -Default "") -cne $sourceCommit) {
		throw "Performance evidence candidate identity does not match the candidate manifest."
	}
	if (-not (Test-HexDigest -Value ([string](Get-PropertyValue -Object $performance -Name "executable_sha256" -Default "")) -Length 64)) {
		throw "Performance evidence has no valid executable SHA-256."
	}
	if (@(Get-PropertyValue -Object $performance -Name "not_measured" -Default @()).Count -ne 0 -or
		-not (Test-AllBooleanGatesPassed -GateObject (Get-PropertyValue -Object $performance -Name "gates"))) {
		throw "Performance evidence contains failed or unmeasured gates."
	}
	$gateResults.Add((New-GateResult -Id "performance" -Status passed -Reason "All locked Windows QML performance-v2 gates passed." -EvidencePath $performanceEvidence.file.path -EvidenceSha256 $performanceEvidence.file.sha256))
} catch {
	$gateResults.Add((New-GateResult -Id "performance" -Status failed -Reason $_.Exception.Message -EvidencePath ([IO.Path]::GetFullPath($PerformanceEvidencePath))))
}

try {
	$connectedEvidence = Read-JsonEvidence -Path $ConnectedEvidencePath
	$connected = $connectedEvidence.document
	$connectedCandidate = Get-PropertyValue -Object $connected -Name "candidate"
	$connectedMatrix = Get-PropertyValue -Object $connected -Name "matrix"
	$connectedScenarios = @(Get-PropertyValue -Object $connected -Name "scenarios" -Default @())
	$connectedEligible = Get-PropertyValue -Object $connected -Name "eligible"
	if ((Get-PropertyValue -Object $connected -Name "schema_version" -Default 0) -ne 1 -or
		(Get-PropertyValue -Object $connected -Name "artifact_kind" -Default "") -cne "connected_product_release_evidence" -or
		(Get-PropertyValue -Object $connected -Name "gate_id" -Default "") -cne "windows-qml-connected-product" -or
		(Get-PropertyValue -Object $connected -Name "policy_id" -Default "") -cne "community-release") {
		throw "Connected evidence must use the schema-v1 community-release connected product policy."
	}
	if ([string](Get-PropertyValue -Object $connectedCandidate -Name "id" -Default "") -cne $candidateId -or
		[string](Get-PropertyValue -Object $connectedCandidate -Name "source_revision" -Default "") -cne $sourceCommit -or
		[string](Get-PropertyValue -Object $connectedCandidate -Name "executable_sha256" -Default "") -cne $candidateExecutableSha256) {
		throw "Connected evidence candidate identity does not match the exact release candidate."
	}
	if ($connectedEligible -isnot [bool] -or -not [bool]$connectedEligible -or
		(Get-PropertyValue -Object $connected -Name "status" -Default "") -cne "passed" -or
		@(Get-PropertyValue -Object $connected -Name "blocking_scenario_ids" -Default @()).Count -ne 0) {
		throw "Connected community-release evidence is not eligible or contains blocking scenarios."
	}
	if ($connectedScenarios.Count -eq 0 -or
		@($connectedScenarios | Where-Object { (Get-PropertyValue -Object $_ -Name "status" -Default "") -cne "passed" }).Count -ne 0) {
		throw "Connected community-release evidence must contain only passed scenario results."
	}
	if (-not (Test-HexDigest -Value ([string](Get-PropertyValue -Object $connectedMatrix -Name "sha256" -Default "")) -Length 64)) {
		throw "Connected evidence has no valid release-matrix SHA-256."
	}
	$gateResults.Add((New-GateResult -Id "connected_product" -Status passed -Reason "Connected community-release policy evidence is eligible, fully passed and bound to the exact candidate." -EvidencePath $connectedEvidence.file.path -EvidenceSha256 $connectedEvidence.file.sha256))
} catch {
	$gateResults.Add((New-GateResult -Id "connected_product" -Status failed -Reason $_.Exception.Message -EvidencePath ([IO.Path]::GetFullPath($ConnectedEvidencePath))))
}

try {
	$artifactEvidence = Get-EvidenceFile -Path $WindowsArtifactEvidencePath
	$artifactPaths = @(Get-Content -LiteralPath $artifactEvidence.path | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })
	if ($artifactPaths.Count -eq 0) { throw "Windows artifact evidence is empty." }
	$resolvedArtifactPaths = [Collections.Generic.List[string]]::new()
	foreach ($artifactPath in $artifactPaths) {
		$resolvedArtifactPaths.Add((Resolve-Path -LiteralPath $artifactPath).Path)
	}
	if ($resolvedArtifactPaths -notcontains $candidateExecutablePath) {
		throw "Windows artifact evidence does not list the candidate executable."
	}
	foreach ($requiredName in @("mumble-updater.exe", "mumble-screen-helper.exe")) {
		if (@($resolvedArtifactPaths | Where-Object { [IO.Path]::GetFileName($_) -ieq $requiredName }).Count -eq 0) {
			throw "Windows artifact evidence is missing $requiredName."
		}
	}
	$candidateMsiArtifacts = @($resolvedArtifactPaths | Where-Object { [IO.Path]::GetFileName($_) -match '(?i)client.*\.msi$' })
	if ($candidateMsiArtifacts.Count -ne 1) {
		throw "Windows artifact evidence must contain exactly one client MSI; found $($candidateMsiArtifacts.Count)."
	}
	$candidateArtifactMsiPath = $candidateMsiArtifacts[0]
	$candidateArtifactMsiSha256 = (Get-FileHash -LiteralPath $candidateArtifactMsiPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$runtimeManifestPath = Join-Path (Split-Path -Parent $candidateExecutablePath) "runtime-manifest.json"
	$runtimeManifest = (Read-JsonEvidence -Path $runtimeManifestPath).document
	$mumbleEntries = @((Get-PropertyValue -Object $runtimeManifest -Name "files" -Default @()) | Where-Object {
		[string](Get-PropertyValue -Object $_ -Name "path" -Default "") -ceq "mumble.exe"
	})
	if ((Get-PropertyValue -Object $runtimeManifest -Name "schema_version" -Default 0) -ne 1 -or
		$mumbleEntries.Count -ne 1 -or
		[string](Get-PropertyValue -Object $mumbleEntries[0] -Name "sha256" -Default "") -cne $candidateExecutableSha256) {
		throw "Staged runtime manifest does not bind the candidate executable hash."
	}
	$gateResults.Add((New-GateResult -Id "windows_artifacts" -Status passed -Reason "Windows stage, updater, helper, MSI and runtime manifest are bound to the candidate executable." -EvidencePath $artifactEvidence.path -EvidenceSha256 $artifactEvidence.sha256))
} catch {
	$gateResults.Add((New-GateResult -Id "windows_artifacts" -Status failed -Reason $_.Exception.Message -EvidencePath ([IO.Path]::GetFullPath($WindowsArtifactEvidencePath))))
}

try {
	if ([string]::IsNullOrWhiteSpace($MsiPayloadEvidencePath)) {
		throw "MsiPayloadEvidencePath is required for release qualification."
	}
	$msiPayloadEvidenceFile = Read-JsonEvidence -Path $MsiPayloadEvidencePath
	$msiPayloadEvidence = Assert-WindowsMsiPayloadEvidence -Evidence $msiPayloadEvidenceFile.document `
		-CandidateClientMsi $candidateArtifactMsiPath -CandidateExecutable $candidateExecutablePath
	$gateResults.Add((New-GateResult -Id "windows_msi_payload" -Status passed `
		-Reason "The candidate MSI embeds the exact frozen candidate mumble.exe SHA-256." `
		-EvidencePath $msiPayloadEvidenceFile.file.path -EvidenceSha256 $msiPayloadEvidenceFile.file.sha256))
} catch {
	$gateResults.Add((New-GateResult -Id "windows_msi_payload" -Status failed -Reason $_.Exception.Message `
		-EvidencePath $(if ([string]::IsNullOrWhiteSpace($MsiPayloadEvidencePath)) { "" } else { [IO.Path]::GetFullPath($MsiPayloadEvidencePath) })))
}

try {
	$installerEvidence = Read-JsonEvidence -Path $InstallerUpgradeEvidencePath
	$installer = $installerEvidence.document
	$installerCandidate = Get-PropertyValue -Object $installer -Name "candidate"
	$installerInputs = Get-PropertyValue -Object $installer -Name "inputs"
	$installerCandidateManifest = Get-PropertyValue -Object $installerInputs -Name "candidate_manifest"
	$installerMsiPayload = Get-PropertyValue -Object $installerInputs -Name "candidate_msi_payload"
	$previousMsi = Get-PropertyValue -Object $installerInputs -Name "previous_client_msi"
	$candidateMsi = Get-PropertyValue -Object $installerInputs -Name "candidate_client_msi"
	$verification = Get-PropertyValue -Object $installer -Name "verification"
	$guidPattern = '^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$'
	if ((Get-PropertyValue -Object $installer -Name "schema_version" -Default 0) -ne 1 -or
		(Get-PropertyValue -Object $installer -Name "artifact_kind" -Default "") -cne "windows_installer_upgrade_evidence" -or
		(Get-PropertyValue -Object $installer -Name "gate_id" -Default "") -cne "windows-qml-installer-upgrade") {
		throw "Installer evidence must be a schema-v1 full installer-upgrade run; ContractOnly output is not releasable."
	}
	$installerEligible = Get-PropertyValue -Object $installer -Name "eligible"
	if ((Get-PropertyValue -Object $installer -Name "status" -Default "") -cne "passed" -or
		$installerEligible -isnot [bool] -or -not [bool]$installerEligible) {
		throw "Installer upgrade evidence is not eligible."
	}
	if ([string](Get-PropertyValue -Object $installerCandidate -Name "id" -Default "") -cne $candidateId -or
		[string](Get-PropertyValue -Object $installerCandidate -Name "source_revision" -Default "") -cne $sourceCommit -or
		[string](Get-PropertyValue -Object $installerCandidate -Name "executable_sha256" -Default "") -cne $candidateExecutableSha256) {
		throw "Installer evidence candidate identity does not match the exact release candidate."
	}
	if ([string](Get-PropertyValue -Object $installerCandidateManifest -Name "sha256" -Default "") -cne $candidateEvidence.file.sha256) {
		throw "Installer evidence is not bound to the exact candidate manifest file."
	}
	$installerMsiPayloadEvidenceFile = Get-PropertyValue -Object $installerMsiPayload -Name "evidence_file"
	if ($null -eq $msiPayloadEvidenceFile -or $null -eq $installerMsiPayloadEvidenceFile -or
		[string](Get-PropertyValue -Object $installerMsiPayloadEvidenceFile -Name "sha256" -Default "") -cne $msiPayloadEvidenceFile.file.sha256 -or
		[string](Get-PropertyValue -Object $installerMsiPayload -Name "candidate_client_msi_sha256" -Default "") -cne $candidateArtifactMsiSha256 -or
		[string](Get-PropertyValue -Object $installerMsiPayload -Name "candidate_executable_sha256" -Default "") -cne $candidateExecutableSha256 -or
		[string](Get-PropertyValue -Object $installerMsiPayload -Name "embedded_executable_sha256" -Default "") -cne $candidateExecutableSha256 -or
		(Get-PropertyValue -Object $installerMsiPayload -Name "exact_executable_sha256_match") -isnot [bool] -or
		-not [bool](Get-PropertyValue -Object $installerMsiPayload -Name "exact_executable_sha256_match")) {
		throw "Installer upgrade evidence did not consume the exact qualified MSI payload preflight."
	}
	foreach ($msi in @($previousMsi, $candidateMsi)) {
		$msiPath = [string](Get-PropertyValue -Object $msi -Name "path" -Default "")
		$msiSha256 = ([string](Get-PropertyValue -Object $msi -Name "sha256" -Default "")).ToLowerInvariant()
		$productCode = [string](Get-PropertyValue -Object $msi -Name "product_code" -Default "")
		$upgradeCode = [string](Get-PropertyValue -Object $msi -Name "upgrade_code" -Default "")
		if (-not (Test-HexDigest -Value $msiSha256 -Length 64) -or $productCode -cnotmatch $guidPattern -or
			$upgradeCode -cnotmatch $guidPattern) {
			throw "Installer evidence contains invalid MSI identity metadata."
		}
		$resolvedMsiPath = (Resolve-Path -LiteralPath $msiPath -ErrorAction Stop).Path
		if ((Get-FileHash -LiteralPath $resolvedMsiPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $msiSha256) {
			throw "Installer evidence MSI hash no longer matches: $resolvedMsiPath"
		}
	}
	if ([string](Get-PropertyValue -Object $previousMsi -Name "product_code" -Default "") -ceq
		[string](Get-PropertyValue -Object $candidateMsi -Name "product_code" -Default "") -or
		[string](Get-PropertyValue -Object $previousMsi -Name "upgrade_code" -Default "") -cne
		[string](Get-PropertyValue -Object $candidateMsi -Name "upgrade_code" -Default "")) {
		throw "Installer evidence does not describe a distinct-product upgrade within one UpgradeCode."
	}
	$candidateMsiPath = (Resolve-Path -LiteralPath ([string](Get-PropertyValue -Object $candidateMsi -Name "path" -Default "")) -ErrorAction Stop).Path
	if ($resolvedArtifactPaths -notcontains $candidateMsiPath) {
		throw "Qualified candidate MSI is not present in Windows artifact evidence."
	}
	if (-not (Test-AllBooleanGatesPassed -GateObject $verification)) {
		throw "Installer upgrade evidence contains a failed or missing verification result."
	}
	$gateResults.Add((New-GateResult -Id "windows_installer_upgrade" -Status passed `
		-Reason "Previous install, candidate upgrade, exact executable, isolated profile, QML readiness and uninstall are qualified." `
		-EvidencePath $installerEvidence.file.path -EvidenceSha256 $installerEvidence.file.sha256))
} catch {
	$gateResults.Add((New-GateResult -Id "windows_installer_upgrade" -Status failed -Reason $_.Exception.Message `
		-EvidencePath ([IO.Path]::GetFullPath($InstallerUpgradeEvidencePath))))
}

$failedRequired = @($gateResults | Where-Object { $_.required -and $_.status -eq "failed" } | ForEach-Object { $_.id })
$missingRequired = @($gateResults | Where-Object { $_.required -and $_.status -eq "missing" } | ForEach-Object { $_.id })
$ready = $failedRequired.Count -eq 0 -and $missingRequired.Count -eq 0
$report = [ordered]@{
	schema_version = 1
	gate_id = "windows-qml-community-release-v1"
	evaluated_at_utc = [DateTime]::UtcNow.ToString("o")
	candidate = [ordered]@{
		candidate_id = if ([string]::IsNullOrWhiteSpace($candidateId)) { $null } else { $candidateId }
		candidate_kind = if ([string]::IsNullOrWhiteSpace($candidateKind)) { $null } else { $candidateKind }
		source_commit = if ([string]::IsNullOrWhiteSpace($sourceCommit)) { $null } else { $sourceCommit }
		source_worktree_sha256 = if ([string]::IsNullOrWhiteSpace($sourceWorktreeSha256)) { $null } else { $sourceWorktreeSha256 }
		source_clean = $sourceClean
		executable_path = if ([string]::IsNullOrWhiteSpace($candidateExecutablePath)) { $null } else { $candidateExecutablePath }
		executable_sha256 = if ([string]::IsNullOrWhiteSpace($candidateExecutableSha256)) { $null } else { $candidateExecutableSha256 }
	}
	gates = @($gateResults)
	failed_required_gates = $failedRequired
	missing_required_gates = $missingRequired
	ready_for_community_release = $ready
}

$outputFile = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFile
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputFile -Encoding utf8
$report | ConvertTo-Json -Depth 10

if (-not $ready) {
	throw "Windows community release gate failed closed (failed: $($failedRequired -join ', '); missing: $($missingRequired -join ', ')). Report: $outputFile"
}
