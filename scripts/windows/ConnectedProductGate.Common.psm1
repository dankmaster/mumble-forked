Set-StrictMode -Version Latest

$script:ConnectedProductClassifications = @('required', 'extended', 'manual')
$script:ConnectedProductAssertionOperators = @(
	'equal', 'notEqual', 'truthy', 'falsy', 'contains', 'countAtLeast', 'matches', 'exists', 'absent'
)
$script:ConnectedProductCleanupConditions = @('always', 'on-success', 'on-failure')

function ConvertTo-ConnectedProductArray {
	param([AllowNull()]$Value)

	if ($null -eq $Value) { return @() }
	if ($Value -is [Array]) { return @($Value) }
	if ($Value -is [Collections.IEnumerable] -and $Value -isnot [string]) { return @($Value) }
	return @($Value)
}

function Get-ConnectedProductProperty {
	param(
		[AllowNull()]$Object,
		[Parameter(Mandatory = $true)][string]$Name,
		$DefaultValue = $null
	)

	if ($null -eq $Object) { return $DefaultValue }
	if ($Object -is [Collections.IDictionary]) {
		return $Object.Contains($Name) ? $Object[$Name] : $DefaultValue
	}
	$property = $Object.PSObject.Properties[$Name]
	return $property ? $property.Value : $DefaultValue
}

function Test-ConnectedProductProperty {
	param(
		[AllowNull()]$Object,
		[Parameter(Mandatory = $true)][string]$Name
	)

	if ($null -eq $Object) { return $false }
	if ($Object -is [Collections.IDictionary]) { return $Object.Contains($Name) }
	return $null -ne $Object.PSObject.Properties[$Name]
}

function Get-ConnectedProductFileSha256 {
	param([Parameter(Mandatory = $true)][string]$Path)

	$resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
	if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
		throw "Connected product evidence input is not a file: $Path"
	}
	return (Get-FileHash -LiteralPath $resolved.Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Resolve-ConnectedProductEvidenceReference {
	param(
		[Parameter(Mandatory = $true)][string]$Reference,
		[Parameter(Mandatory = $true)][string]$BasePath
	)

	if ([string]::IsNullOrWhiteSpace($Reference) -or $Reference.Length -gt 512) {
		throw 'Connected product evidence contains an invalid evidence reference.'
	}
	$baseFullPath = [IO.Path]::GetFullPath($BasePath)
	$referencedPath = if ([IO.Path]::IsPathRooted($Reference)) {
		[IO.Path]::GetFullPath($Reference)
	} else {
		[IO.Path]::GetFullPath((Join-Path $baseFullPath $Reference))
	}
	$resolved = Resolve-Path -LiteralPath $referencedPath -ErrorAction Stop
	if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
		throw "Connected product evidence reference is not a file: $Reference"
	}
	return $resolved.Path
}

function Get-ConnectedProductEmbeddedCandidateBinding {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$Reference
	)

	try {
		$document = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
	} catch {
		throw "Connected product JSON evidence '$Reference' is invalid: $($_.Exception.Message)"
	}
	$binding = Get-ConnectedProductProperty -Object $document -Name 'candidate_binding'
	if ($null -eq $binding) {
		throw "Connected product JSON evidence '$Reference' has no candidate_binding."
	}
	return [pscustomobject]@{
		candidate_id = [string](Get-ConnectedProductProperty -Object $binding -Name 'candidate_id' -DefaultValue '')
		source_revision = ([string](Get-ConnectedProductProperty -Object $binding -Name 'source_revision' -DefaultValue '')).ToLowerInvariant()
		executable_sha256 = ([string](Get-ConnectedProductProperty -Object $binding -Name 'executable_sha256' -DefaultValue '')).ToLowerInvariant()
	}
}

function New-ConnectedProductEvidenceArtifact {
	param(
		[Parameter(Mandatory = $true)][string]$Reference,
		[Parameter(Mandatory = $true)][string]$EvidenceBasePath,
		[Parameter(Mandatory = $true)][string]$CandidateId,
		[Parameter(Mandatory = $true)][string]$SourceRevision,
		[Parameter(Mandatory = $true)][string]$ExecutableSha256
	)

	$resolvedPath = Resolve-ConnectedProductEvidenceReference -Reference $Reference -BasePath $EvidenceBasePath
	$file = Get-Item -LiteralPath $resolvedPath -ErrorAction Stop
	$extension = [IO.Path]::GetExtension($file.Name)
	$bindingMode = 'gate-manifest'
	if ($extension -ieq '.json') {
		$binding = Get-ConnectedProductEmbeddedCandidateBinding -Path $resolvedPath -Reference $Reference
		if ($binding.candidate_id -cne $CandidateId) {
			throw "Connected product JSON evidence '$Reference' candidate id mismatch. Expected '$CandidateId', found '$($binding.candidate_id)'."
		}
		if ($binding.source_revision -cne $SourceRevision.ToLowerInvariant()) {
			throw "Connected product JSON evidence '$Reference' source revision mismatch."
		}
		if ($binding.executable_sha256 -cne $ExecutableSha256.ToLowerInvariant()) {
			throw "Connected product JSON evidence '$Reference' executable SHA-256 mismatch."
		}
		$bindingMode = 'embedded'
	}

	return [pscustomobject][ordered]@{
		reference = $Reference
		file_name = $file.Name
		size_bytes = [long]$file.Length
		sha256 = Get-ConnectedProductFileSha256 -Path $resolvedPath
		binding = [ordered]@{
			mode = $bindingMode
			candidate_id = $CandidateId
			source_revision = $SourceRevision.ToLowerInvariant()
			executable_sha256 = $ExecutableSha256.ToLowerInvariant()
		}
	}
}

function Test-ConnectedProductReleaseMatrix {
	param([Parameter(Mandatory = $true)]$Matrix)

	$errors = [Collections.Generic.List[string]]::new()
	$schemaVersion = Get-ConnectedProductProperty -Object $Matrix -Name 'schema_version' -DefaultValue 0
	if ([int]$schemaVersion -ne 1) {
		$errors.Add("schema_version must be 1.")
	}

	$gateId = [string](Get-ConnectedProductProperty -Object $Matrix -Name 'gate_id' -DefaultValue '')
	if ($gateId -notmatch '^[a-z0-9]+(?:[.-][a-z0-9]+)*$') {
		$errors.Add("gate_id must be a stable lowercase identifier.")
	}
	if ([string](Get-ConnectedProductProperty -Object $Matrix -Name 'frontend_contract' -DefaultValue '') -ne
		'typed-controllers-and-models') {
		$errors.Add("frontend_contract must be 'typed-controllers-and-models'.")
	}

	$policies = @(ConvertTo-ConnectedProductArray (Get-ConnectedProductProperty -Object $Matrix -Name 'policies'))
	if ($policies.Count -eq 0) { $errors.Add('At least one release policy is required.') }
	$policyIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($policy in $policies) {
		$policyId = [string](Get-ConnectedProductProperty -Object $policy -Name 'id' -DefaultValue '')
		if ($policyId -notmatch '^[a-z0-9]+(?:[.-][a-z0-9]+)*$') {
			$errors.Add("Policy '$policyId' has an invalid id.")
		} elseif (-not $policyIds.Add($policyId)) {
			$errors.Add("Policy id '$policyId' is duplicated.")
		}
		$requiredClassifications = @(ConvertTo-ConnectedProductArray (
			Get-ConnectedProductProperty -Object $policy -Name 'required_classifications'))
		if ($requiredClassifications.Count -eq 0) {
			$errors.Add("Policy '$policyId' has no required_classifications.")
		}
		foreach ($classification in $requiredClassifications) {
			if ([string]$classification -notin $script:ConnectedProductClassifications) {
				$errors.Add("Policy '$policyId' contains unknown classification '$classification'.")
			}
		}
	}

	$defaultPolicyId = [string](Get-ConnectedProductProperty -Object $Matrix -Name 'default_policy_id' -DefaultValue '')
	if ([string]::IsNullOrWhiteSpace($defaultPolicyId) -or -not $policyIds.Contains($defaultPolicyId)) {
		$errors.Add("default_policy_id must reference a declared policy.")
	}

	$scenarios = @(ConvertTo-ConnectedProductArray (Get-ConnectedProductProperty -Object $Matrix -Name 'scenarios'))
	if ($scenarios.Count -eq 0) { $errors.Add('At least one connected product scenario is required.') }
	$scenarioIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($scenario in $scenarios) {
		$scenarioId = [string](Get-ConnectedProductProperty -Object $scenario -Name 'id' -DefaultValue '')
		if ($scenarioId -notmatch '^[a-z0-9]+(?:[.-][a-z0-9]+)*$') {
			$errors.Add("Scenario '$scenarioId' has an invalid id.")
		} elseif (-not $scenarioIds.Add($scenarioId)) {
			$errors.Add("Scenario id '$scenarioId' is duplicated.")
		}

		$classification = [string](Get-ConnectedProductProperty -Object $scenario -Name 'classification' -DefaultValue '')
		if ($classification -notin $script:ConnectedProductClassifications) {
			$errors.Add("Scenario '$scenarioId' has unknown classification '$classification'.")
		}

		$commands = @(ConvertTo-ConnectedProductArray (Get-ConnectedProductProperty -Object $scenario -Name 'commands'))
		if ($commands.Count -eq 0) { $errors.Add("Scenario '$scenarioId' has no commands.") }
		$commandIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
		foreach ($command in $commands) {
			$commandId = [string]$command
			if ($commandId -notmatch '^[A-Za-z][A-Za-z0-9.-]*$') {
				$errors.Add("Scenario '$scenarioId' contains invalid command '$commandId'.")
			} elseif (-not $commandIds.Add($commandId)) {
				$errors.Add("Scenario '$scenarioId' repeats command '$commandId'.")
			}
		}

		$assertions = @(ConvertTo-ConnectedProductArray (Get-ConnectedProductProperty -Object $scenario -Name 'assertions'))
		if ($assertions.Count -eq 0) { $errors.Add("Scenario '$scenarioId' has no assertions.") }
		$assertionIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
		foreach ($assertion in $assertions) {
			$assertionId = [string](Get-ConnectedProductProperty -Object $assertion -Name 'id' -DefaultValue '')
			if ($assertionId -notmatch '^[a-z0-9]+(?:[.-][a-z0-9]+)*$') {
				$errors.Add("Scenario '$scenarioId' has invalid assertion id '$assertionId'.")
			} elseif (-not $assertionIds.Add($assertionId)) {
				$errors.Add("Scenario '$scenarioId' repeats assertion '$assertionId'.")
			}
			$path = [string](Get-ConnectedProductProperty -Object $assertion -Name 'path' -DefaultValue '')
			if ($path -notmatch '^[A-Za-z][A-Za-z0-9_.\[\]-]*$') {
				$errors.Add("Scenario '$scenarioId' assertion '$assertionId' has invalid path '$path'.")
			}
			$operator = [string](Get-ConnectedProductProperty -Object $assertion -Name 'operator' -DefaultValue '')
			if ($operator -notin $script:ConnectedProductAssertionOperators) {
				$errors.Add("Scenario '$scenarioId' assertion '$assertionId' has unknown operator '$operator'.")
			}
			if ($operator -in @('equal', 'notEqual', 'contains', 'countAtLeast', 'matches') -and
				-not (Test-ConnectedProductProperty -Object $assertion -Name 'expected')) {
				$errors.Add("Scenario '$scenarioId' assertion '$assertionId' requires expected.")
			}
		}

		$cleanup = @(ConvertTo-ConnectedProductArray (Get-ConnectedProductProperty -Object $scenario -Name 'cleanup'))
		if ($cleanup.Count -eq 0) { $errors.Add("Scenario '$scenarioId' has no cleanup.") }
		foreach ($cleanupStep in $cleanup) {
			$cleanupCommand = [string](Get-ConnectedProductProperty -Object $cleanupStep -Name 'command' -DefaultValue '')
			if ($cleanupCommand -notmatch '^[A-Za-z][A-Za-z0-9.-]*$') {
				$errors.Add("Scenario '$scenarioId' contains invalid cleanup command '$cleanupCommand'.")
			}
			$condition = [string](Get-ConnectedProductProperty -Object $cleanupStep -Name 'when' -DefaultValue '')
			if ($condition -notin $script:ConnectedProductCleanupConditions) {
				$errors.Add("Scenario '$scenarioId' cleanup '$cleanupCommand' has invalid when '$condition'.")
			}
			if (-not (Test-ConnectedProductProperty -Object $cleanupStep -Name 'best_effort') -or
				(Get-ConnectedProductProperty -Object $cleanupStep -Name 'best_effort') -isnot [bool]) {
				$errors.Add("Scenario '$scenarioId' cleanup '$cleanupCommand' must declare boolean best_effort.")
			}
		}
	}

	return [pscustomobject]@{
		valid = $errors.Count -eq 0
		errors = $errors.ToArray()
	}
}

function Assert-ConnectedProductReleaseMatrix {
	param([Parameter(Mandatory = $true)]$Matrix)

	$result = Test-ConnectedProductReleaseMatrix -Matrix $Matrix
	if (-not $result.valid) {
		throw "Invalid connected product release matrix:`n - $($result.errors -join "`n - ")"
	}
	return $Matrix
}

function Import-ConnectedProductReleaseMatrix {
	param([Parameter(Mandatory = $true)][string]$Path)

	$resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
	if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
		throw "Connected product release matrix is not a file: $Path"
	}
	try {
		$matrix = Get-Content -LiteralPath $resolved.Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
	} catch {
		throw "Connected product release matrix is not valid JSON: $($_.Exception.Message)"
	}
	return Assert-ConnectedProductReleaseMatrix -Matrix $matrix
}

function New-ConnectedProductGateEvidenceManifest {
	param(
		[Parameter(Mandatory = $true)][string]$MatrixPath,
		[Parameter(Mandatory = $true)][string]$CandidateId,
		[Parameter(Mandatory = $true)][string]$SourceRevision,
		[Parameter(Mandatory = $true)][string]$ExecutablePath,
		[Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$ScenarioResults,
		[string]$PolicyId = '',
		[string]$EvidenceBasePath = '',
		[string]$OutputPath = ''
	)

	if ($CandidateId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
		throw "CandidateId must be a stable filesystem-safe identifier."
	}
	if ($SourceRevision -notmatch '^[0-9a-fA-F]{7,64}$') {
		throw "SourceRevision must be a 7-64 character hexadecimal source revision."
	}

	$resolvedMatrix = (Resolve-Path -LiteralPath $MatrixPath -ErrorAction Stop).Path
	$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath -ErrorAction Stop).Path
	if ([string]::IsNullOrWhiteSpace($EvidenceBasePath)) {
		$EvidenceBasePath = (Get-Location).Path
	}
	$resolvedEvidenceBasePath = [IO.Path]::GetFullPath($EvidenceBasePath)
	if (-not (Test-Path -LiteralPath $resolvedEvidenceBasePath -PathType Container)) {
		throw "EvidenceBasePath is not a directory: $EvidenceBasePath"
	}
	$executableInfo = Get-Item -LiteralPath $resolvedExecutable -ErrorAction Stop
	$executableSha256 = Get-ConnectedProductFileSha256 -Path $resolvedExecutable
	$normalizedSourceRevision = $SourceRevision.ToLowerInvariant()
	$matrix = Import-ConnectedProductReleaseMatrix -Path $resolvedMatrix
	if ([string]::IsNullOrWhiteSpace($PolicyId)) {
		$PolicyId = [string]$matrix.default_policy_id
	}
	$policy = @(ConvertTo-ConnectedProductArray $matrix.policies | Where-Object { [string]$_.id -eq $PolicyId })
	if ($policy.Count -ne 1) { throw "Unknown connected product release policy '$PolicyId'." }

	$scenarios = @(ConvertTo-ConnectedProductArray $matrix.scenarios)
	$scenarioById = @{}
	foreach ($scenario in $scenarios) { $scenarioById[[string]$scenario.id] = $scenario }
	$resultById = @{}
	foreach ($result in @(ConvertTo-ConnectedProductArray $ScenarioResults)) {
		$resultId = [string](Get-ConnectedProductProperty -Object $result -Name 'id' -DefaultValue '')
		if (-not $scenarioById.ContainsKey($resultId)) {
			throw "Scenario result '$resultId' is not declared by the release matrix."
		}
		if ($resultById.ContainsKey($resultId)) { throw "Scenario result '$resultId' is duplicated." }
		$status = [string](Get-ConnectedProductProperty -Object $result -Name 'status' -DefaultValue '')
		if ($status -notin @('passed', 'failed', 'skipped')) {
			throw "Scenario result '$resultId' has invalid status '$status'."
		}
		$duration = [long](Get-ConnectedProductProperty -Object $result -Name 'duration_ms' -DefaultValue 0)
		if ($duration -lt 0) { throw "Scenario result '$resultId' has a negative duration_ms." }
		$evidenceReferences = @(ConvertTo-ConnectedProductArray (
			Get-ConnectedProductProperty -Object $result -Name 'evidence_refs'))
		if ($status -eq 'passed' -and $evidenceReferences.Count -eq 0) {
			throw "Passed scenario result '$resultId' has no evidence references."
		}
		$evidenceArtifacts = [Collections.Generic.List[object]]::new()
		$resolvedEvidencePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
		foreach ($reference in $evidenceReferences) {
			$referenceText = [string]$reference
			if ([string]::IsNullOrWhiteSpace($referenceText) -or $referenceText.Length -gt 512) {
				throw "Scenario result '$resultId' contains an invalid evidence reference."
			}
			$resolvedReference = Resolve-ConnectedProductEvidenceReference -Reference $referenceText `
				-BasePath $resolvedEvidenceBasePath
			if (-not $resolvedEvidencePaths.Add($resolvedReference)) {
				throw "Scenario result '$resultId' repeats evidence reference '$referenceText'."
			}
			$evidenceArtifacts.Add((New-ConnectedProductEvidenceArtifact -Reference $referenceText `
				-EvidenceBasePath $resolvedEvidenceBasePath -CandidateId $CandidateId `
				-SourceRevision $normalizedSourceRevision -ExecutableSha256 $executableSha256))
		}
		$resultById[$resultId] = [ordered]@{
			id = $resultId
			classification = [string]$scenarioById[$resultId].classification
			status = $status
			duration_ms = $duration
			evidence_refs = @($evidenceReferences | ForEach-Object { [string]$_ })
			evidence_artifacts = $evidenceArtifacts.ToArray()
		}
	}

	$requiredClassifications = @($policy[0].required_classifications | ForEach-Object { [string]$_ })
	$normalizedResults = [Collections.Generic.List[object]]::new()
	$blockingScenarioIds = [Collections.Generic.List[string]]::new()
	$nonBlockingFailedScenarioIds = [Collections.Generic.List[string]]::new()
	foreach ($scenario in $scenarios) {
		$scenarioId = [string]$scenario.id
		$isRequired = [string]$scenario.classification -in $requiredClassifications
		if ($resultById.ContainsKey($scenarioId)) {
			$result = $resultById[$scenarioId]
			$normalizedResults.Add([pscustomobject]$result)
			if ($isRequired -and $result.status -ne 'passed') {
				$blockingScenarioIds.Add($scenarioId)
			} elseif (-not $isRequired -and $result.status -eq 'failed') {
				$nonBlockingFailedScenarioIds.Add($scenarioId)
			}
		} elseif ($isRequired) {
			$normalizedResults.Add([pscustomobject][ordered]@{
				id = $scenarioId
				classification = [string]$scenario.classification
				status = 'not-run'
				duration_ms = 0
				evidence_refs = @()
				evidence_artifacts = @()
			})
			$blockingScenarioIds.Add($scenarioId)
		}
	}

	$eligible = $blockingScenarioIds.Count -eq 0
	$manifest = [ordered]@{
		schema_version = 1
		artifact_kind = 'connected_product_release_evidence'
		gate_id = [string]$matrix.gate_id
		policy_id = $PolicyId
		status = $eligible ? 'passed' : 'failed'
		eligible = $eligible
		generated_at_utc = [DateTime]::UtcNow.ToString('o')
		candidate = [ordered]@{
			id = $CandidateId
			source_revision = $normalizedSourceRevision
			executable_name = $executableInfo.Name
			executable_size_bytes = [long]$executableInfo.Length
			executable_sha256 = $executableSha256
		}
		matrix = [ordered]@{
			file_name = [IO.Path]::GetFileName($resolvedMatrix)
			sha256 = Get-ConnectedProductFileSha256 -Path $resolvedMatrix
		}
		required_classifications = $requiredClassifications
		blocking_scenario_ids = $blockingScenarioIds.ToArray()
		non_blocking_failed_scenario_ids = $nonBlockingFailedScenarioIds.ToArray()
		scenarios = $normalizedResults.ToArray()
	}

	if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
		$fullOutputPath = [IO.Path]::GetFullPath($OutputPath)
		$outputDirectory = Split-Path -Parent $fullOutputPath
		if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
			New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
		}
		$json = $manifest | ConvertTo-Json -Depth 20
		[IO.File]::WriteAllText($fullOutputPath, $json + [Environment]::NewLine,
			[Text.UTF8Encoding]::new($false))
	}

	return [pscustomobject]$manifest
}

Export-ModuleMember -Function Get-ConnectedProductFileSha256,
	Test-ConnectedProductReleaseMatrix,
	Assert-ConnectedProductReleaseMatrix,
	Import-ConnectedProductReleaseMatrix,
	New-ConnectedProductGateEvidenceManifest
