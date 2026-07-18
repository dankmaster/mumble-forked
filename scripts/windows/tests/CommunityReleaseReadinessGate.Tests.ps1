$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$assemblerPath = Join-Path $repoRoot 'scripts\windows\invoke-community-release-readiness-gate.ps1'

function Write-ReadinessJson {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)]$Document)
	$Document | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding utf8
}

function New-ReadinessFixture {
	param([Parameter(Mandatory = $true)][string]$Root)
	New-Item -ItemType Directory -Path $Root -Force | Out-Null
	$executable = Join-Path $Root 'mumble.exe'
	[IO.File]::WriteAllBytes($executable, [byte[]](1, 3, 3, 7))
	$executableSha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
	$sourceRevision = 'a' * 40
	$worktreeSha256 = 'b' * 64
	$candidateId = 'community-release-fixture'
	$candidatePath = Join-Path $Root 'candidate.json'
	Write-ReadinessJson -Path $candidatePath -Document ([ordered]@{
		schema_version = 1
		candidate_id = $candidateId
		candidate_kind = 'release'
		source = [ordered]@{ git_sha = $sourceRevision; worktree_sha256 = $worktreeSha256; clean = $true }
		windows = [ordered]@{ executable_path = $executable; executable_sha256 = $executableSha256 }
	})
	$candidateSha256 = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash.ToLowerInvariant()
	$requiredGateIds = @(
		'candidate_manifest', 'release_candidate_kind', 'clean_source_tree', 'visual_accessibility',
		'performance', 'connected_product', 'windows_artifacts', 'windows_msi_payload',
		'windows_installer_upgrade'
	)
	$windowsPath = Join-Path $Root 'windows.json'
	$windowsGates = @($requiredGateIds | ForEach-Object {
		[ordered]@{
			id = $_
			required = $true
			status = 'passed'
			reason = 'fixture'
			evidence_sha256 = $_ -eq 'candidate_manifest' ? $candidateSha256 : ('c' * 64)
		}
	})
	Write-ReadinessJson -Path $windowsPath -Document ([ordered]@{
		schema_version = 1
		gate_id = 'windows-qml-community-release-v1'
		candidate = [ordered]@{
			candidate_id = $candidateId
			candidate_kind = 'release'
			source_commit = $sourceRevision
			source_worktree_sha256 = $worktreeSha256
			source_clean = $true
			executable_path = $executable
			executable_sha256 = $executableSha256
		}
		gates = $windowsGates
		failed_required_gates = @()
		missing_required_gates = @()
		ready_for_community_release = $true
	})
	$linuxPath = Join-Path $Root 'linux.json'
	Write-ReadinessJson -Path $linuxPath -Document ([ordered]@{
		schema_version = 1
		candidate_git_sha = $sourceRevision
		build_number = 42
		server_sha256 = 'd' * 64
		cmake_cache_sha256 = 'f' * 64
		configuration = 'linux-x86_64-static-release-tests'
		client = $false
		server = $true
		build_contract = [ordered]@{
			build_type = 'Release'
			client = $false
			screen_helper = $false
			server = $true
			static = $true
			tests = $true
		}
		tests = [ordered]@{
			status = 'passed'
			total = 25
			failures = 0
			errors = 0
			skipped = 1
			result_file = 'linux-murmur-ctest.xml'
			result_sha256 = 'e' * 64
		}
	})
	return [pscustomobject]@{
		candidate = $candidatePath
		windows = $windowsPath
		linux = $linuxPath
		output = Join-Path $Root 'promotion.json'
		candidate_id = $candidateId
		source_revision = $sourceRevision
		executable_sha256 = $executableSha256
	}
}

function Invoke-ReadinessFixture {
	param([Parameter(Mandatory = $true)]$Fixture)
	$hostExecutable = (Get-Process -Id $PID).Path
	& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $assemblerPath `
		-CandidateManifestPath $Fixture.candidate `
		-WindowsCommunityEvidencePath $Fixture.windows `
		-LinuxMurmurEvidencePath $Fixture.linux `
		-OutputPath $Fixture.output 2>&1 | Out-Null
	return $LASTEXITCODE
}

Describe 'Three-track community release readiness assembler' {
	BeforeEach {
		$script:root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-readiness-' + [Guid]::NewGuid().ToString('N'))
		$script:fixture = New-ReadinessFixture -Root $script:root
	}

	AfterEach {
		Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
	}

	It 'writes promotion evidence only when the clean candidate and both tracks pass' {
		(Invoke-ReadinessFixture -Fixture $script:fixture) | Should Be 0
		$promotion = Get-Content -LiteralPath $script:fixture.output -Raw | ConvertFrom-Json
		$promotion.schema_version | Should Be 1
		$promotion.artifact_kind | Should Be 'community_release_promotion_evidence'
		$promotion.gate_id | Should Be 'windows-qml-linux-murmur-community-release'
		$promotion.status | Should Be 'passed'
		$promotion.promotion_eligible | Should Be $true
		$promotion.promotion_id | Should Be $script:fixture.candidate_id
		$promotion.candidate.source_revision | Should Be $script:fixture.source_revision
		$promotion.candidate.windows_executable_sha256 | Should Be $script:fixture.executable_sha256
		$promotion.linux_murmur.configuration | Should Be 'linux-x86_64-static-release-tests'
		$promotion.linux_murmur.cmake_cache_sha256 | Should Be ('f' * 64)
		@($promotion.tracks | Where-Object status -eq 'passed').Count | Should Be 3
		@($promotion.failed_required_tracks).Count | Should Be 0
	}

	It 'fails closed for a dirty development candidate and candidate-only Linux evidence' {
		$candidate = Get-Content -LiteralPath $script:fixture.candidate -Raw | ConvertFrom-Json
		$candidate.candidate_kind = 'development'
		$candidate.source.clean = $false
		Write-ReadinessJson -Path $script:fixture.candidate -Document $candidate
		$linux = Get-Content -LiteralPath $script:fixture.linux -Raw | ConvertFrom-Json
		$linux.tests.status = 'not-run'
		$linux.tests.total = 0
		$linux.tests.PSObject.Properties.Remove('result_file')
		$linux.tests.PSObject.Properties.Remove('result_sha256')
		Write-ReadinessJson -Path $script:fixture.linux -Document $linux

		(Invoke-ReadinessFixture -Fixture $script:fixture) | Should Not Be 0
		$promotion = Get-Content -LiteralPath $script:fixture.output -Raw | ConvertFrom-Json
		$promotion.promotion_eligible | Should Be $false
		(@($promotion.failed_required_tracks) -contains 'release_candidate') | Should Be $true
		(@($promotion.failed_required_tracks) -contains 'windows_client') | Should Be $true
		(@($promotion.failed_required_tracks) -contains 'linux_murmur') | Should Be $true
		($promotion.tracks | Where-Object id -eq 'linux_murmur').reason | Should Match 'tests-not-run is candidate-only'
	}

	It 'rejects cross-candidate Windows identity and Linux source evidence' {
		$windows = Get-Content -LiteralPath $script:fixture.windows -Raw | ConvertFrom-Json
		$windows.candidate.candidate_id = 'other-candidate'
		$windows.candidate.executable_sha256 = 'f' * 64
		Write-ReadinessJson -Path $script:fixture.windows -Document $windows
		$linux = Get-Content -LiteralPath $script:fixture.linux -Raw | ConvertFrom-Json
		$linux.candidate_git_sha = '0' * 40
		Write-ReadinessJson -Path $script:fixture.linux -Document $linux

		(Invoke-ReadinessFixture -Fixture $script:fixture) | Should Not Be 0
		$promotion = Get-Content -LiteralPath $script:fixture.output -Raw | ConvertFrom-Json
		(@($promotion.failed_required_tracks) -contains 'windows_client') | Should Be $true
		(@($promotion.failed_required_tracks) -contains 'linux_murmur') | Should Be $true
		($promotion.tracks | Where-Object id -eq 'windows_client').reason | Should Match 'exact clean release candidate'
		($promotion.tracks | Where-Object id -eq 'linux_murmur').reason | Should Match 'candidate source revision'
	}

	It 'rejects Linux evidence without the exact single-binary static test contract' {
		$linux = Get-Content -LiteralPath $script:fixture.linux -Raw | ConvertFrom-Json
		$linux.build_contract.static = $false
		Write-ReadinessJson -Path $script:fixture.linux -Document $linux

		(Invoke-ReadinessFixture -Fixture $script:fixture) | Should Not Be 0
		$promotion = Get-Content -LiteralPath $script:fixture.output -Raw | ConvertFrom-Json
		(@($promotion.failed_required_tracks) -contains 'linux_murmur') | Should Be $true
		($promotion.tracks | Where-Object id -eq 'linux_murmur').reason | Should Match 'single-binary server-only, static, tested Release build contract'
	}

	It 'rejects legacy Linux evidence without a bound CMake cache and static test configuration' {
		$linux = Get-Content -LiteralPath $script:fixture.linux -Raw | ConvertFrom-Json
		$linux.PSObject.Properties.Remove('cmake_cache_sha256')
		$linux.configuration = 'linux-x86_64-shared-tests'
		Write-ReadinessJson -Path $script:fixture.linux -Document $linux

		(Invoke-ReadinessFixture -Fixture $script:fixture) | Should Not Be 0
		$promotion = Get-Content -LiteralPath $script:fixture.output -Raw | ConvertFrom-Json
		(@($promotion.failed_required_tracks) -contains 'linux_murmur') | Should Be $true
		($promotion.tracks | Where-Object id -eq 'linux_murmur').reason | Should Match 'required static Murmur test artifact'
	}

	It 'writes failed promotion evidence when a required track file is missing' {
		Remove-Item -LiteralPath $script:fixture.linux -Force
		(Invoke-ReadinessFixture -Fixture $script:fixture) | Should Not Be 0
		$promotion = Get-Content -LiteralPath $script:fixture.output -Raw | ConvertFrom-Json
		$promotion.status | Should Be 'failed'
		(@($promotion.failed_required_tracks) -contains 'linux_murmur') | Should Be $true
		($promotion.tracks | Where-Object id -eq 'linux_murmur').reason | Should Match 'does not exist'
	}

	It 'parses and contains no publish, deploy or git mutation path' {
		$tokens = $null
		$parseErrors = $null
		[void][Management.Automation.Language.Parser]::ParseFile($assemblerPath, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		$source = Get-Content -LiteralPath $assemblerPath -Raw
		$source | Should Match 'community_release_promotion_evidence'
		$source | Should Match 'windows-qml-community-release-v1'
		$source | Should Match 'tests-not-run is candidate-only'
		$source | Should Not Match '(?im)^\s*(git|gh)\s+'
		$source | Should Not Match '(?i)publish|deploy'
	}
}
