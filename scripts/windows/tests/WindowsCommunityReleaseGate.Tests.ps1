$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$gateScript = Join-Path $repoRoot 'scripts\windows\invoke-windows-community-release-gate.ps1'

function Write-TestJson {
	param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)]$Document)
	$Document | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Invoke-TestReleaseGate {
	param([Parameter(Mandatory = $true)][string]$Root)
	$hostExecutable = (Get-Process -Id $PID).Path
	& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $gateScript `
		-CandidateManifestPath (Join-Path $Root 'candidate.json') `
		-VisualEvidencePath (Join-Path $Root 'visual.json') `
		-PerformanceEvidencePath (Join-Path $Root 'performance.json') `
		-ConnectedEvidencePath (Join-Path $Root 'connected.json') `
		-WindowsArtifactEvidencePath (Join-Path $Root 'windows-artifacts.txt') `
		-MsiPayloadEvidencePath (Join-Path $Root 'msi-payload.json') `
		-InstallerUpgradeEvidencePath (Join-Path $Root 'installer-upgrade.json') `
		-OutputPath (Join-Path $Root 'release-gate.json') 2>&1 | Out-Null
	return $LASTEXITCODE
}

Describe 'Windows community release gate scaffolding' {
	BeforeEach {
		$script:root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-release-gate-' + [Guid]::NewGuid().ToString('N'))
		$script:stage = Join-Path $script:root 'stage'
		New-Item -ItemType Directory -Path $script:stage | Out-Null
		$script:sourceCommit = 'a' * 40
		$script:candidateId = 'windows-qml-test-candidate'

		$candidateExecutable = Join-Path $script:stage 'mumble.exe'
		[IO.File]::WriteAllBytes($candidateExecutable, [byte[]](1, 2, 3, 4))
		$candidateExecutableSha256 = (Get-FileHash -LiteralPath $candidateExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
		$updater = Join-Path $script:stage 'mumble-updater.exe'
		$helper = Join-Path $script:stage 'mumble-screen-helper.exe'
		$msi = Join-Path $script:root 'mumble_client-1.7.1-x64.msi'
		$previousMsi = Join-Path $script:root 'mumble_client-1.7.0-x64.msi'
		[IO.File]::WriteAllBytes($updater, [byte[]](5))
		[IO.File]::WriteAllBytes($helper, [byte[]](6))
		[IO.File]::WriteAllBytes($msi, [byte[]](7))
		[IO.File]::WriteAllBytes($previousMsi, [byte[]](8))
		$msiSha256 = (Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash.ToLowerInvariant()

		$script:msiPayloadEvidencePath = Join-Path $script:root 'msi-payload.json'
		Write-TestJson -Path $script:msiPayloadEvidencePath -Document ([ordered]@{
			schema_version = 1
			artifact_kind = 'windows_msi_payload_evidence'
			gate_id = 'windows-qml-msi-payload'
			status = 'passed'
			eligible = $true
			candidate_client_msi = [ordered]@{ path = $msi; sha256 = $msiSha256 }
			candidate_executable = [ordered]@{ path = $candidateExecutable; sha256 = $candidateExecutableSha256 }
			embedded_payload = [ordered]@{ path = 'Program Files/Mumble/client/mumble.exe'; sha256 = $candidateExecutableSha256; size = 4 }
			verification = [ordered]@{ exact_executable_sha256_match = $true }
		})
		$script:msiPayloadEvidenceSha256 = (Get-FileHash -LiteralPath $script:msiPayloadEvidencePath -Algorithm SHA256).Hash.ToLowerInvariant()

		Write-TestJson -Path (Join-Path $script:stage 'runtime-manifest.json') -Document ([ordered]@{
			schema_version = 1
			files = @([ordered]@{ path = 'mumble.exe'; size = 4; sha256 = $candidateExecutableSha256 })
		})
		@($candidateExecutable, $updater, $helper, $msi) |
			Set-Content -LiteralPath (Join-Path $script:root 'windows-artifacts.txt') -Encoding utf8

		Write-TestJson -Path (Join-Path $script:root 'candidate.json') -Document ([ordered]@{
			schema_version = 1
			candidate_id = $script:candidateId
			candidate_kind = 'release'
			source = [ordered]@{ git_sha = $script:sourceCommit; worktree_sha256 = ('b' * 64); clean = $true }
			windows = [ordered]@{ executable_path = $candidateExecutable; executable_sha256 = $candidateExecutableSha256 }
		})
		$candidateManifestSha256 = (Get-FileHash -LiteralPath (Join-Path $script:root 'candidate.json') -Algorithm SHA256).Hash.ToLowerInvariant()
		Write-TestJson -Path (Join-Path $script:root 'visual.json') -Document ([ordered]@{
			schema_version = 1
			frontend = 'qml'
			mode = 'gate'
			source_git_sha = $script:sourceCommit
			executable_sha256 = ('c' * 64)
			cases = @([ordered]@{ id = 'desktop'; image_sha256 = ('d' * 64); accessibility_sha256 = ('e' * 64) })
		})
		Write-TestJson -Path (Join-Path $script:root 'performance.json') -Document ([ordered]@{
			schema_version = 2
			contract_id = 'windows-qml-performance-v2'
			candidate_id = $script:candidateId
			source_commit = $script:sourceCommit
			executable_sha256 = ('f' * 64)
			not_measured = @()
			gates = [ordered]@{ exactly_five_runs_measured = $true; executable_unchanged_during_runs = $true }
		})
		Write-TestJson -Path (Join-Path $script:root 'connected.json') -Document ([ordered]@{
			schema_version = 1
			artifact_kind = 'connected_product_release_evidence'
			gate_id = 'windows-qml-connected-product'
			policy_id = 'community-release'
			status = 'passed'
			eligible = $true
			candidate = [ordered]@{
				id = $script:candidateId
				source_revision = $script:sourceCommit
				executable_sha256 = $candidateExecutableSha256
			}
			matrix = [ordered]@{ sha256 = ('1' * 64) }
			blocking_scenario_ids = @()
			scenarios = @([ordered]@{ id = 'connect.saved-server'; classification = 'required'; status = 'passed' })
		})
		Write-TestJson -Path (Join-Path $script:root 'installer-upgrade.json') -Document ([ordered]@{
			schema_version = 1
			artifact_kind = 'windows_installer_upgrade_evidence'
			gate_id = 'windows-qml-installer-upgrade'
			status = 'passed'
			eligible = $true
			candidate = [ordered]@{
				id = $script:candidateId
				source_revision = $script:sourceCommit
				executable_sha256 = $candidateExecutableSha256
			}
			inputs = [ordered]@{
				candidate_manifest = [ordered]@{ path = (Join-Path $script:root 'candidate.json'); sha256 = $candidateManifestSha256 }
				candidate_msi_payload = [ordered]@{
					evidence_file = [ordered]@{ path = $script:msiPayloadEvidencePath; sha256 = $script:msiPayloadEvidenceSha256 }
					candidate_client_msi_sha256 = $msiSha256
					candidate_executable_sha256 = $candidateExecutableSha256
					embedded_executable_sha256 = $candidateExecutableSha256
					exact_executable_sha256_match = $true
				}
				previous_client_msi = [ordered]@{
					path = $previousMsi
					sha256 = (Get-FileHash -LiteralPath $previousMsi -Algorithm SHA256).Hash.ToLowerInvariant()
					product_code = '{11111111-1111-1111-1111-111111111111}'
					upgrade_code = '{AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA}'
				}
				candidate_client_msi = [ordered]@{
					path = $msi
					sha256 = (Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash.ToLowerInvariant()
					product_code = '{22222222-2222-2222-2222-222222222222}'
					upgrade_code = '{AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA}'
				}
			}
			verification = [ordered]@{
				candidate_msi_payload_hash_match = $true
				previous_install_verified = $true
				profile_seed_unchanged_after_previous_install = $true
				candidate_upgrade_verified = $true
				installed_executable_hash_match = $true
				profile_seed_unchanged_after_candidate_upgrade = $true
				runtime_ready = $true
				profile_seed_unchanged_after_runtime = $true
				candidate_uninstalled = $true
				installed_executable_removed = $true
				profile_seed_unchanged_after_uninstall = $true
			}
		})
	}

	AfterEach {
		Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
	}

	It 'accepts a complete hash-bound release candidate including installer upgrade evidence' {
		(Invoke-TestReleaseGate -Root $script:root) | Should Be 0
		$report = Get-Content -LiteralPath (Join-Path $script:root 'release-gate.json') -Raw | ConvertFrom-Json
		$report.gate_id | Should Be 'windows-qml-community-release-v1'
		$report.ready_for_community_release | Should Be $true
		@($report.failed_required_gates).Count | Should Be 0
		@($report.missing_required_gates).Count | Should Be 0
		@($report.gates | Where-Object status -eq 'passed').Count | Should Be 9
		($report.gates | Where-Object id -eq 'windows_msi_payload').status | Should Be 'passed'
		($report.gates | Where-Object id -eq 'windows_installer_upgrade').status | Should Be 'passed'
	}

	It 'rejects MSI payload evidence whose embedded executable hash differs from the candidate' {
		$payload = Get-Content -LiteralPath $script:msiPayloadEvidencePath -Raw | ConvertFrom-Json
		$payload.embedded_payload.sha256 = '0' * 64
		Write-TestJson -Path $script:msiPayloadEvidencePath -Document $payload

		(Invoke-TestReleaseGate -Root $script:root) | Should Not Be 0
		$report = Get-Content -LiteralPath (Join-Path $script:root 'release-gate.json') -Raw | ConvertFrom-Json
		(@($report.failed_required_gates) -contains 'windows_msi_payload') | Should Be $true
		($report.gates | Where-Object id -eq 'windows_msi_payload').reason |
			Should Match 'embedded mumble\.exe matches the frozen candidate executable'
	}

	It 'rejects candidate-only visual evidence and a dirty release source' {
		$candidate = Get-Content -LiteralPath (Join-Path $script:root 'candidate.json') -Raw | ConvertFrom-Json
		$candidate.source.clean = $false
		Write-TestJson -Path (Join-Path $script:root 'candidate.json') -Document $candidate
		$visual = Get-Content -LiteralPath (Join-Path $script:root 'visual.json') -Raw | ConvertFrom-Json
		$visual.mode = 'candidate-only'
		Write-TestJson -Path (Join-Path $script:root 'visual.json') -Document $visual

		(Invoke-TestReleaseGate -Root $script:root) | Should Not Be 0
		$report = Get-Content -LiteralPath (Join-Path $script:root 'release-gate.json') -Raw | ConvertFrom-Json
		(@($report.failed_required_gates) -contains 'clean_source_tree') | Should Be $true
		(@($report.failed_required_gates) -contains 'visual_accessibility') | Should Be $true
	}

	It 'rejects connected evidence from the candidate-only policy' {
		$connected = Get-Content -LiteralPath (Join-Path $script:root 'connected.json') -Raw | ConvertFrom-Json
		$connected.policy_id = 'community-candidate'
		Write-TestJson -Path (Join-Path $script:root 'connected.json') -Document $connected

		(Invoke-TestReleaseGate -Root $script:root) | Should Not Be 0
		$report = Get-Content -LiteralPath (Join-Path $script:root 'release-gate.json') -Raw | ConvertFrom-Json
		(@($report.failed_required_gates) -contains 'connected_product') | Should Be $true
		($report.gates | Where-Object id -eq 'connected_product').reason |
			Should Match 'community-release connected product policy'
	}

	It 'rejects ContractOnly or candidate-mismatched installer evidence' {
		$installer = Get-Content -LiteralPath (Join-Path $script:root 'installer-upgrade.json') -Raw | ConvertFrom-Json
		$installer.artifact_kind = 'windows_installer_upgrade_contract'
		$installer.status = 'contract_only'
		$installer.eligible = $false
		$installer.candidate.id = 'different-candidate'
		Write-TestJson -Path (Join-Path $script:root 'installer-upgrade.json') -Document $installer

		(Invoke-TestReleaseGate -Root $script:root) | Should Not Be 0
		$report = Get-Content -LiteralPath (Join-Path $script:root 'release-gate.json') -Raw | ConvertFrom-Json
		(@($report.failed_required_gates) -contains 'windows_installer_upgrade') | Should Be $true
		($report.gates | Where-Object id -eq 'windows_installer_upgrade').reason |
			Should Match 'ContractOnly output is not releasable'
	}

	It 'parses without errors and names the installer-upgrade gate explicitly' {
		$tokens = $null
		$parseErrors = $null
		$source = Get-Content -LiteralPath $gateScript -Raw
		[void][Management.Automation.Language.Parser]::ParseFile($gateScript, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
		$source | Should Match 'windows_installer_upgrade'
		$source | Should Match 'Final release readiness requires source\.clean=true'
		$source | Should Match 'candidate-only evidence is not releasable'
		$source | Should Match 'connected_product_release_evidence'
		$source | Should Match 'community-release connected product policy'
		$source | Should Match 'InstallerUpgradeEvidencePath'
		$source | Should Match 'MsiPayloadEvidencePath'
		$source | Should Match 'windows_msi_payload'
		$source | Should Match 'windows_installer_upgrade_evidence'
		$source | Should Match 'ContractOnly output is not releasable'
	}
}
