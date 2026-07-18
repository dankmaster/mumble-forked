$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$verifierPath = Join-Path $repoRoot 'scripts\windows\verify-windows-installer-upgrade.ps1'

Describe 'Windows installer upgrade release contract' {
	BeforeAll {
		$script:source = Get-Content -LiteralPath $verifierPath -Raw
	}

	It 'parses without errors' {
		$tokens = $null
		$parseErrors = $null
		[void][Management.Automation.Language.Parser]::ParseFile($verifierPath, [ref]$tokens, [ref]$parseErrors)
		$parseErrors.Count | Should Be 0
	}

	It 'exposes a non-mutating ContractOnly path before platform, admin, COM or MSI work' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-installer-contract-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$output = Join-Path $root 'contract.json'
			$hostExecutable = (Get-Process -Id $PID).Path
			& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $verifierPath `
				-PreviousClientMsi (Join-Path $root 'previous.msi') `
				-CandidateClientMsi (Join-Path $root 'candidate.msi') `
				-CandidateManifestPath (Join-Path $root 'candidate.json') `
				-InstalledExecutablePath (Join-Path $root 'installed\mumble.exe') `
				-ProfileSeedRoot (Join-Path $root 'profile') -OutputPath $output -ContractOnly 2>&1 | Out-Null
			$LASTEXITCODE | Should Be 0
			$contract = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
			$contract.schema_version | Should Be 1
			$contract.artifact_kind | Should Be 'windows_installer_upgrade_contract'
			$contract.gate_id | Should Be 'windows-qml-installer-upgrade'
			$contract.contract_only | Should Be $true
			$contract.eligible | Should Be $false
			(Test-Path -LiteralPath (Join-Path $root 'previous.msi')) | Should Be $false
			(Test-Path -LiteralPath (Join-Path $root 'candidate.msi')) | Should Be $false
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
		}

		$contractIndex = $script:source.IndexOf('if ($ContractOnly)')
		$adminIndex = $script:source.IndexOf('if (-not (Test-IsAdministrator))')
		$comIndex = $script:source.IndexOf('New-Object -ComObject WindowsInstaller.Installer')
		$contractIndex -ge 0 | Should Be $true
		$adminIndex -gt $contractIndex | Should Be $true
		$comIndex -gt $contractIndex | Should Be $true
	}

	It 'requires an explicit isolated profile argument shape even in contract mode' {
		$root = Join-Path ([IO.Path]::GetTempPath()) ('mumble-installer-contract-' + [Guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $root | Out-Null
		try {
			$hostExecutable = (Get-Process -Id $PID).Path
			& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $verifierPath `
				-PreviousClientMsi 'previous.msi' -CandidateClientMsi 'candidate.msi' `
				-CandidateManifestPath 'candidate.json' -InstalledExecutablePath 'mumble.exe' `
				-ConfigPath 'settings.json' -OutputPath (Join-Path $root 'contract.json') -ContractOnly 2>&1 | Out-Null
			$LASTEXITCODE | Should Not Be 0
			(Test-Path -LiteralPath (Join-Path $root 'contract.json')) | Should Be $false
		} finally {
			Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
		}
	}

	It 'reads ProductCode and UpgradeCode through Windows Installer COM and performs silent MSI operations' {
		$script:source | Should Match 'WindowsInstaller\.Installer'
		$script:source | Should Match "PropertyName 'ProductCode'"
		$script:source | Should Match "PropertyName 'UpgradeCode'"
		$script:source | Should Match "'/qn', '/norestart', '/l\*v'"
		$script:source | Should Match "ValidateSet\('install', 'uninstall'\)"
		$script:source | Should Match 'Previous and candidate client MSIs must share one UpgradeCode'
		$script:source | Should Match 'Previous and candidate client MSIs must have distinct ProductCode'
	}

	It 'verifies the embedded candidate executable before any installer operation' {
		$script:source | Should Match 'Get-WindowsMsiPayloadVerification'
		$script:source | Should Match 'Assert-WindowsMsiPayloadEvidence'
		$preflightIndex = $script:source.IndexOf('Assert-WindowsMsiPayloadEvidence')
		$installIndex = $script:source.IndexOf('$operations.previous_install')
		$preflightIndex -ge 0 | Should Be $true
		$installIndex -gt $preflightIndex | Should Be $true
	}

	It 'binds the installed executable and evidence to the exact release candidate and both MSIs' {
		$script:source | Should Match "artifact_kind = 'windows_installer_upgrade_evidence'"
		$script:source | Should Match 'source_revision = \$sourceRevision'
		$script:source | Should Match 'executable_sha256 = \$expectedExecutableSha256'
		$script:source | Should Match 'candidate_manifest = \[ordered\]@\{ path = \$candidateManifestFullPath; sha256 = Get-FileSha256'
		$script:source | Should Match 'candidate_msi_payload = \[ordered\]@\{'
		$script:source | Should Match 'candidate_msi_payload_hash_match'
		$script:source | Should Match 'previous_client_msi = \$previousMetadata'
		$script:source | Should Match 'candidate_client_msi = \$candidateMetadata'
		$script:source | Should Match 'installed_executable_hash_match'
	}

	It 'preserves the source profile seed and starts a cloned profile through QML readiness' {
		$script:source | Should Match 'Get-ProfileSnapshot'
		$script:source | Should Match 'profile_seed_unchanged_after_previous_install'
		$script:source | Should Match 'profile_seed_unchanged_after_candidate_upgrade'
		$script:source | Should Match 'profile_seed_unchanged_after_runtime'
		$script:source | Should Match 'profile_seed_unchanged_after_uninstall'
		$script:source | Should Match "command = 'qmlReadinessState'"
		$script:source | Should Match "frontend -ceq 'qml'"
		$script:source | Should Match 'mainCaptureReady'
		$script:source | Should Match 'Copy-Item -LiteralPath \$profile\.database -Destination \$runtimeDatabasePath'
	}

	It 'requires disposable elevation and attempts product cleanup in finally' {
		$script:source | Should Match 'requires -DisposableRunner on a disposable Windows runner or VM'
		$script:source | Should Match 'requires an elevated administrator token'
		$script:source | Should Match 'finally \{[\s\S]*foreach \(\$metadata in @\(\$candidateMetadata, \$previousMetadata\)\)'
		$script:source | Should Match 'Invoke-MsiOperation -Operation uninstall[\s\S]*-CleanupAttempt'
		$script:source | Should Match 'candidate_uninstalled'
		$script:source | Should Match 'installed_executable_removed'
	}
}
