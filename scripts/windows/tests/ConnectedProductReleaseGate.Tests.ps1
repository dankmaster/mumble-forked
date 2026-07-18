$modulePath = "$PSScriptRoot\..\ConnectedProductGate.Common.psm1"
$matrixPath = "$PSScriptRoot\..\connected-product-release-matrix.json"
$runnerPath = "$PSScriptRoot\..\invoke-connected-product-release-gate.ps1"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$automationSourcePath = Join-Path $repoRoot 'src\mumble\ModernUiAutomationServer.cpp'
Import-Module $modulePath -Force

function Copy-ConnectedProductObject {
	param([Parameter(Mandatory = $true)]$Value)
	return ($Value | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
}

function New-ConnectedProductScenarioResults {
	param(
		[Parameter(Mandatory = $true)]$Matrix,
		[string[]]$Classifications = @('required'),
		[string]$Status = 'passed',
		[string]$EvidenceRoot = $script:contractEvidenceRoot
	)

	return @($Matrix.scenarios | Where-Object { $_.classification -in $Classifications } | ForEach-Object {
		$evidencePath = Join-Path $EvidenceRoot "$($_.id).txt"
		New-Item -ItemType Directory -Path (Split-Path -Parent $evidencePath) -Force | Out-Null
		if (-not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) {
			[IO.File]::WriteAllText($evidencePath, "connected product evidence for $($_.id)`n",
				[Text.UTF8Encoding]::new($false))
		}
		[pscustomobject]@{
			id = [string]$_.id
			status = $Status
			duration_ms = 1
			evidence_refs = @($evidencePath)
		}
	})
}

function Write-ConnectedProductBoundJsonEvidence {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$CandidateId,
		[Parameter(Mandatory = $true)][string]$SourceRevision,
		[Parameter(Mandatory = $true)][string]$ExecutableSha256,
		[string]$ScenarioId = 'fixture'
	)

	New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
	Write-ConnectedProductTestJson -Path $Path -Document ([ordered]@{
		candidate_binding = [ordered]@{
			candidate_id = $CandidateId
			source_revision = $SourceRevision
			executable_sha256 = $ExecutableSha256
		}
		scenario_id = $ScenarioId
		status = 'passed'
	})
}

function Write-ConnectedProductTestJson {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)]$Document)
	[IO.File]::WriteAllText($Path, ($Document | ConvertTo-Json -Depth 30) + [Environment]::NewLine,
		[Text.UTF8Encoding]::new($false))
}

function New-ConnectedProductRunnerFixture {
	param(
		[Parameter(Mandatory = $true)][string]$Root,
		[string]$CandidateKind = 'development',
		[bool]$SourceClean = $false,
		[string[]]$Classifications = @('required')
	)

	New-Item -ItemType Directory -Path $Root -Force | Out-Null
	$executablePath = Join-Path $Root 'mumble.exe'
	[IO.File]::WriteAllBytes($executablePath, [byte[]](11, 22, 33, 44))
	$executableSha256 = Get-ConnectedProductFileSha256 -Path $executablePath
	$candidatePath = Join-Path $Root 'candidate.json'
	$resultsPath = Join-Path $Root 'scenario-results.json'
	$outputPath = Join-Path $Root 'connected-evidence.json'
	Write-ConnectedProductTestJson -Path $candidatePath -Document ([ordered]@{
		schema_version = 1
		candidate_id = 'connected-product-test'
		candidate_kind = $CandidateKind
		source = [ordered]@{ git_sha = ('a' * 40); clean = $SourceClean }
		windows = [ordered]@{
			executable_path = $executablePath
			executable_sha256 = $executableSha256
		}
	})
	$results = @(New-ConnectedProductScenarioResults -Matrix $script:matrix -Classifications $Classifications)
	$evidenceRoot = Join-Path $Root 'evidence'
	foreach ($result in $results) {
		$evidencePath = Join-Path $evidenceRoot "$($result.id).json"
		Write-ConnectedProductBoundJsonEvidence -Path $evidencePath -CandidateId 'connected-product-test' `
			-SourceRevision ('a' * 40) -ExecutableSha256 $executableSha256 -ScenarioId $result.id
		$result.evidence_refs = @($evidencePath)
	}
	Write-ConnectedProductTestJson -Path $resultsPath -Document $results
	return [pscustomobject]@{
		candidate = $candidatePath
		results = $resultsPath
		output = $outputPath
		executable = $executablePath
		executable_sha256 = $executableSha256
	}
}

function Invoke-ConnectedProductTestRunner {
	param(
		[Parameter(Mandatory = $true)]$Fixture,
		[string]$PolicyId = 'community-candidate'
	)

	$hostExecutable = (Get-Process -Id $PID).Path
	& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $runnerPath `
		-CandidateManifestPath $Fixture.candidate -ScenarioResultsPath $Fixture.results `
		-MatrixPath $matrixPath -PolicyId $PolicyId -OutputPath $Fixture.output 2>&1 | Out-Null
	return $LASTEXITCODE
}

Describe 'Connected product release gate contract' {
	BeforeAll {
		$script:contractEvidenceRoot = Join-Path ([IO.Path]::GetTempPath()) (
			'connected-contract-evidence-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $script:contractEvidenceRoot -Force | Out-Null
		$script:matrix = Import-ConnectedProductReleaseMatrix -Path $matrixPath
	}

	AfterAll {
		Remove-Item -LiteralPath $script:contractEvidenceRoot -Recurse -Force -ErrorAction SilentlyContinue
	}

	It 'keeps the production matrix frontend-neutral and structurally valid' {
		$result = Test-ConnectedProductReleaseMatrix -Matrix $script:matrix
		$result.valid | Should Be $true
		@($result.errors).Count | Should Be 0
		$script:matrix.frontend_contract | Should Be 'typed-controllers-and-models'
		@($script:matrix.scenarios | Where-Object classification -eq 'required').Count | Should BeGreaterThan 0
		@($script:matrix.scenarios | Where-Object classification -eq 'extended').Count | Should BeGreaterThan 0
		@($script:matrix.scenarios | Where-Object classification -eq 'manual').Count | Should BeGreaterThan 0

		$source = Get-Content -Raw -LiteralPath $matrixPath
		$source | Should Not Match 'objectName|AutomationId|192\.168\.|localhost|127\.0\.0\.1|MumbleDevClient'
	}

	It 'references only commands exposed by the typed automation endpoint' {
		$automationSource = Get-Content -Raw -LiteralPath $automationSourcePath
		$commands = @(
			$script:matrix.scenarios.commands
			$script:matrix.scenarios.cleanup.command
		) | ForEach-Object { [string]$_ } | Sort-Object -Unique
		foreach ($command in $commands) {
			$automationSource.Contains('QLatin1String("' + $command + '")') | Should Be $true
		}
	}

	It 'requires bidirectional DM for candidates and rich DM capabilities for releases' {
		$requiredDm = @($script:matrix.scenarios | Where-Object id -eq 'direct-message.bidirectional')
		$richDm = @($script:matrix.scenarios | Where-Object id -eq 'direct-message.rich-capabilities')
		$legacyDm = @($script:matrix.scenarios | Where-Object id -eq 'direct-message.rich')
		$requiredDm.Count | Should Be 1
		$requiredDm[0].classification | Should Be 'required'
		$richDm.Count | Should Be 1
		$richDm[0].classification | Should Be 'extended'
		$legacyDm.Count | Should Be 0
		@($richDm[0].assertions | Where-Object id -eq 'content-hydration-available').Count | Should Be 1
		@($richDm[0].assertions | Where-Object id -eq 'reply-available').Count | Should Be 1
		@($richDm[0].assertions | Where-Object id -eq 'reaction-available').Count | Should Be 1
	}

	It 'keeps unavailable rich DM non-blocking for candidates and fail-closed for releases' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-rich-dm-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](4, 2, 4, 2))
			$allResults = @(New-ConnectedProductScenarioResults -Matrix $script:matrix `
				-Classifications @('required', 'extended', 'manual'))
			$richFailure = @($allResults | Where-Object id -eq 'direct-message.rich-capabilities')[0]
			$richFailure.status = 'failed'

			$candidate = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'candidate-rich-dm-unavailable' -SourceRevision ('3' * 40) `
				-ExecutablePath $exePath -ScenarioResults $allResults -PolicyId 'community-candidate'
			$candidate.eligible | Should Be $true
			(@($candidate.non_blocking_failed_scenario_ids) -contains $richFailure.id) | Should Be $true

			$release = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'release-rich-dm-unavailable' -SourceRevision ('4' * 40) `
				-ExecutablePath $exePath -ScenarioResults $allResults -PolicyId 'community-release'
			$release.eligible | Should Be $false
			(@($release.blocking_scenario_ids) -contains $richFailure.id) | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'rejects duplicate scenario ids, unknown classifications and missing commands' {
		foreach ($mutation in @('duplicate', 'classification', 'commands')) {
			$copy = Copy-ConnectedProductObject -Value $script:matrix
			switch ($mutation) {
				'duplicate' { $copy.scenarios[1].id = $copy.scenarios[0].id }
				'classification' { $copy.scenarios[0].classification = 'optional' }
				'commands' { $copy.scenarios[0].commands = @() }
			}
			$result = Test-ConnectedProductReleaseMatrix -Matrix $copy
			$result.valid | Should Be $false
			@($result.errors).Count | Should BeGreaterThan 0
		}
	}

	It 'rejects malformed assertions and cleanup steps' {
		$badAssertion = Copy-ConnectedProductObject -Value $script:matrix
		$badAssertion.scenarios[0].assertions[0].operator = 'execute-javascript'
		(Test-ConnectedProductReleaseMatrix -Matrix $badAssertion).valid | Should Be $false

		$missingExpected = Copy-ConnectedProductObject -Value $script:matrix
		$missingExpected.scenarios[0].assertions[1].PSObject.Properties.Remove('expected')
		(Test-ConnectedProductReleaseMatrix -Matrix $missingExpected).valid | Should Be $false

		$badCleanup = Copy-ConnectedProductObject -Value $script:matrix
		$badCleanup.scenarios[0].cleanup[0].when = 'eventually'
		(Test-ConnectedProductReleaseMatrix -Matrix $badCleanup).valid | Should Be $false

		$missingCleanupPolicy = Copy-ConnectedProductObject -Value $script:matrix
		$missingCleanupPolicy.scenarios[0].cleanup[0].PSObject.Properties.Remove('best_effort')
		(Test-ConnectedProductReleaseMatrix -Matrix $missingCleanupPolicy).valid | Should Be $false
	}

	It 'creates candidate evidence bound to source, executable and matrix hashes' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](1, 3, 3, 7))
			$outputPath = Join-Path $root 'evidence.json'
			$results = New-ConnectedProductScenarioResults -Matrix $script:matrix

			$evidence = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'community-20260718.1' -SourceRevision ('a' * 40) `
				-ExecutablePath $exePath -ScenarioResults $results -OutputPath $outputPath

			$evidence.status | Should Be 'passed'
			$evidence.eligible | Should Be $true
			$evidence.candidate.source_revision | Should Be ('a' * 40)
			$evidence.candidate.executable_sha256 | Should Be (Get-ConnectedProductFileSha256 $exePath)
			$evidence.matrix.sha256 | Should Be (Get-ConnectedProductFileSha256 $matrixPath)
			$firstScenario = @($evidence.scenarios)[0]
			$firstArtifact = @($firstScenario.evidence_artifacts)[0]
			$firstArtifact.sha256 | Should Be (Get-ConnectedProductFileSha256 $firstArtifact.reference)
			$firstArtifact.binding.mode | Should Be 'gate-manifest'
			$firstArtifact.binding.candidate_id | Should Be 'community-20260718.1'
			Test-Path -LiteralPath $outputPath | Should Be $true
			$persisted = Get-Content -Raw -LiteralPath $outputPath | ConvertFrom-Json
			$persisted.candidate.executable_sha256 | Should Be $evidence.candidate.executable_sha256
			($persisted.PSObject.Properties.Name -contains 'executable_path') | Should Be $false
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'requires every passed evidence reference to exist and captures its digest' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-evidence-ref-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](6, 2, 6, 4))
			$result = (New-ConnectedProductScenarioResults -Matrix $script:matrix)[0]
			$result.evidence_refs = @((Join-Path $root 'missing.json'))
			$threw = $false
			try {
				New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId 'missing-ref' `
					-SourceRevision ('7' * 40) -ExecutablePath $exePath -ScenarioResults @($result) | Out-Null
			} catch { $threw = $true }
			$threw | Should Be $true

			$result.evidence_refs = @()
			$threw = $false
			try {
				New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId 'empty-ref' `
					-SourceRevision ('7' * 40) -ExecutablePath $exePath -ScenarioResults @($result) | Out-Null
			} catch { $threw = $true }
			$threw | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'accepts matching embedded JSON candidate binding and rejects mismatches' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-json-binding-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$candidateId = 'json-bound-candidate'
			$sourceRevision = '8' * 40
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](8, 6, 7, 5, 3, 0, 9))
			$exeSha = Get-ConnectedProductFileSha256 -Path $exePath
			$jsonPath = Join-Path $root 'raw-report.json'
			Write-ConnectedProductBoundJsonEvidence -Path $jsonPath -CandidateId $candidateId `
				-SourceRevision $sourceRevision -ExecutableSha256 $exeSha
			$result = (New-ConnectedProductScenarioResults -Matrix $script:matrix)[0]
			$result.evidence_refs = @($jsonPath)

			$evidence = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId $candidateId `
				-SourceRevision $sourceRevision -ExecutablePath $exePath -ScenarioResults @($result)
			$artifact = @($evidence.scenarios)[0].evidence_artifacts[0]
			$artifact.binding.mode | Should Be 'embedded'
			$artifact.sha256 | Should Be (Get-ConnectedProductFileSha256 -Path $jsonPath)

			Write-ConnectedProductBoundJsonEvidence -Path $jsonPath -CandidateId 'different-candidate' `
				-SourceRevision $sourceRevision -ExecutableSha256 $exeSha
			$threw = $false
			try {
				New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId $candidateId `
					-SourceRevision $sourceRevision -ExecutablePath $exePath -ScenarioResults @($result) | Out-Null
			} catch { $threw = $true }
			$threw | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'fails closed when a required scenario is missing, skipped or failed' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-fail-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](2, 4, 6, 8))
			$allRequired = @(New-ConnectedProductScenarioResults -Matrix $script:matrix)

			$missing = @($allRequired | Select-Object -Skip 1)
			$missingEvidence = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'missing' -SourceRevision ('b' * 40) -ExecutablePath $exePath -ScenarioResults $missing
			$missingEvidence.status | Should Be 'failed'
			$missingEvidence.eligible | Should Be $false
			(@($missingEvidence.blocking_scenario_ids) -contains $allRequired[0].id) | Should Be $true
			$missingRow = @($missingEvidence.scenarios | Where-Object id -eq $allRequired[0].id)[0]
			$missingRow.status | Should Be 'not-run'

			$skipped = @(New-ConnectedProductScenarioResults -Matrix $script:matrix)
			$skipped[0].status = 'skipped'
			(New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId 'skipped' `
				-SourceRevision ('c' * 40) -ExecutablePath $exePath -ScenarioResults $skipped).eligible | Should Be $false

			$failed = @(New-ConnectedProductScenarioResults -Matrix $script:matrix)
			$failed[0].status = 'failed'
			(New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId 'failed' `
				-SourceRevision ('d' * 40) -ExecutablePath $exePath -ScenarioResults $failed).eligible | Should Be $false
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'requires extended and manual evidence for the community release policy' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-release-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](9, 8, 7, 6))
			$requiredOnly = New-ConnectedProductScenarioResults -Matrix $script:matrix
			$candidate = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'candidate' -SourceRevision ('e' * 40) -ExecutablePath $exePath `
				-ScenarioResults $requiredOnly -PolicyId 'community-candidate'
			$candidate.eligible | Should Be $true

			$releaseMissing = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'release-missing' -SourceRevision ('e' * 40) -ExecutablePath $exePath `
				-ScenarioResults $requiredOnly -PolicyId 'community-release'
			$releaseMissing.eligible | Should Be $false
			@($releaseMissing.blocking_scenario_ids).Count | Should BeGreaterThan 0

			$allResults = New-ConnectedProductScenarioResults -Matrix $script:matrix `
				-Classifications @('required', 'extended', 'manual')
			$release = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'release-complete' -SourceRevision ('e' * 40) -ExecutablePath $exePath `
				-ScenarioResults $allResults -PolicyId 'community-release'
			$release.eligible | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'reports optional failures without blocking candidate policy and blocks them under release policy' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-policy-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](7, 7, 7, 7))
			$allResults = @(New-ConnectedProductScenarioResults -Matrix $script:matrix `
				-Classifications @('required', 'extended', 'manual'))
			$extendedFailure = @($allResults | Where-Object {
				$id = [string]$_.id
				@($script:matrix.scenarios | Where-Object { $_.id -eq $id -and $_.classification -eq 'extended' }).Count -gt 0
			})[0]
			$extendedFailure.status = 'failed'

			$candidate = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'candidate-optional-failure' -SourceRevision ('1' * 40) `
				-ExecutablePath $exePath -ScenarioResults $allResults -PolicyId 'community-candidate'
			$candidate.eligible | Should Be $true
			@($candidate.blocking_scenario_ids).Count | Should Be 0
			(@($candidate.non_blocking_failed_scenario_ids) -contains $extendedFailure.id) | Should Be $true
			(@($candidate.scenarios | Where-Object id -eq $extendedFailure.id)[0].status) | Should Be 'failed'

			$release = New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath `
				-CandidateId 'release-extended-failure' -SourceRevision ('2' * 40) `
				-ExecutablePath $exePath -ScenarioResults $allResults -PolicyId 'community-release'
			$release.eligible | Should Be $false
			(@($release.blocking_scenario_ids) -contains $extendedFailure.id) | Should Be $true
			@($release.non_blocking_failed_scenario_ids).Count | Should Be 0
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}

	It 'rejects unknown and duplicate result ids before writing evidence' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('connected-gate-invalid-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$exePath = Join-Path $root 'mumble.exe'
			[IO.File]::WriteAllBytes($exePath, [byte[]](5, 5, 5))
			$unknownThrew = $false
			try {
				New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId 'unknown' `
					-SourceRevision ('f' * 40) -ExecutablePath $exePath `
					-ScenarioResults @([pscustomobject]@{ id = 'unknown.scenario'; status = 'passed' }) | Out-Null
			} catch { $unknownThrew = $true }
			$unknownThrew | Should Be $true

			$first = (New-ConnectedProductScenarioResults -Matrix $script:matrix)[0]
			$duplicateThrew = $false
			try {
				New-ConnectedProductGateEvidenceManifest -MatrixPath $matrixPath -CandidateId 'duplicate' `
					-SourceRevision ('f' * 40) -ExecutablePath $exePath -ScenarioResults @($first, $first) | Out-Null
			} catch { $duplicateThrew = $true }
			$duplicateThrew | Should Be $true
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force
		}
	}
}

Describe 'Connected product release gate runner' {
	BeforeEach {
		$script:runnerRoot = Join-Path ([IO.Path]::GetTempPath()) (
			'connected-runner-' + [Guid]::NewGuid().ToString('N'))
	}

	AfterEach {
		Remove-Item -LiteralPath $script:runnerRoot -Recurse -Force -ErrorAction SilentlyContinue
	}

	It 'accepts a dirty development candidate under the community candidate policy' {
		$fixture = New-ConnectedProductRunnerFixture -Root $script:runnerRoot `
			-CandidateKind 'development' -SourceClean $false
		(Invoke-ConnectedProductTestRunner -Fixture $fixture -PolicyId 'community-candidate') | Should Be 0
		$evidence = Get-Content -Raw -LiteralPath $fixture.output | ConvertFrom-Json
		$evidence.eligible | Should Be $true
		$evidence.status | Should Be 'passed'
		$evidence.candidate.kind | Should Be 'development'
		$evidence.candidate.source_clean | Should Be $false
		$evidence.executable_sha256 | Should Be $fixture.executable_sha256
		$artifact = @($evidence.scenarios)[0].evidence_artifacts[0]
		$artifact.binding.mode | Should Be 'embedded'
		$artifact.binding.candidate_id | Should Be 'connected-product-test'
		$artifact.sha256 | Should Be (Get-ConnectedProductFileSha256 -Path $artifact.reference)
	}

	It 'fails closed when a referenced raw evidence file is missing' {
		$fixture = New-ConnectedProductRunnerFixture -Root $script:runnerRoot
		$results = @(Get-Content -Raw -LiteralPath $fixture.results | ConvertFrom-Json)
		Remove-Item -LiteralPath $results[0].evidence_refs[0] -Force
		(Invoke-ConnectedProductTestRunner -Fixture $fixture) | Should Not Be 0
		Test-Path -LiteralPath $fixture.output | Should Be $false
	}

	It 'fails closed when raw JSON evidence is bound to another candidate' {
		$fixture = New-ConnectedProductRunnerFixture -Root $script:runnerRoot
		$results = @(Get-Content -Raw -LiteralPath $fixture.results | ConvertFrom-Json)
		$evidencePath = [string]$results[0].evidence_refs[0]
		$raw = Get-Content -Raw -LiteralPath $evidencePath | ConvertFrom-Json
		$raw.candidate_binding.executable_sha256 = 'f' * 64
		Write-ConnectedProductTestJson -Path $evidencePath -Document $raw
		(Invoke-ConnectedProductTestRunner -Fixture $fixture) | Should Not Be 0
		Test-Path -LiteralPath $fixture.output | Should Be $false
	}

	It 'rejects an executable whose bytes no longer match the candidate hash' {
		$fixture = New-ConnectedProductRunnerFixture -Root $script:runnerRoot
		[IO.File]::WriteAllBytes($fixture.executable, [byte[]](99, 88, 77))
		(Invoke-ConnectedProductTestRunner -Fixture $fixture) | Should Not Be 0
		Test-Path -LiteralPath $fixture.output | Should Be $false
	}

	It 'writes failed evidence and exits one when a required scenario is missing' {
		$fixture = New-ConnectedProductRunnerFixture -Root $script:runnerRoot
		$results = @(Get-Content -Raw -LiteralPath $fixture.results | ConvertFrom-Json | Select-Object -Skip 1)
		Write-ConnectedProductTestJson -Path $fixture.results -Document $results
		(Invoke-ConnectedProductTestRunner -Fixture $fixture) | Should Be 1
		$evidence = Get-Content -Raw -LiteralPath $fixture.output | ConvertFrom-Json
		$evidence.eligible | Should Be $false
		$evidence.status | Should Be 'failed'
		@($evidence.blocking_scenario_ids).Count | Should BeGreaterThan 0
	}

	It 'rejects a dirty source even when it is labelled as a release candidate' {
		$fixture = New-ConnectedProductRunnerFixture -Root $script:runnerRoot `
			-CandidateKind 'release' -SourceClean $false `
			-Classifications @('required', 'extended', 'manual')
		(Invoke-ConnectedProductTestRunner -Fixture $fixture -PolicyId 'community-release') | Should Not Be 0
		Test-Path -LiteralPath $fixture.output | Should Be $false
	}
}
